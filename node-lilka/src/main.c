#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "app_config.h"
#include "board_pins.h"
#include "mcp2515/mcp2515.h"

static const char *TAG = "app";

static const mcp2515_config_t s_cfg = {
    .host = SPI3_HOST,
    .mosi = CAN_PIN_MOSI,
    .miso = CAN_PIN_MISO,
    .sclk = CAN_PIN_SCLK,
    .cs = CAN_PIN_CS,
    .clock_hz = 1 * 1000 * 1000,
    .osc = MCP2515_OSC_8MHZ,
};

static mcp2515_t s_can;
static bool s_can_started = false;
static bool s_can_ready = false;
static uint32_t s_last_rx_ms = 0;
static uint32_t s_rx_count = 0;
static uint8_t s_last_rx_counter = 0;
static uint8_t s_cmd_seq = 0;
static bool s_key_prev = false;
static bool s_key_stable = false;
static uint32_t s_key_change_ms = 0;

static void key_init(void) {
    const gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << KEY_A_PIN,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    ESP_ERROR_CHECK(gpio_config(&cfg));

    s_key_prev = (gpio_get_level(KEY_A_PIN) == 0);
    s_key_stable = s_key_prev;
    s_key_change_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

static bool key_a_just_pressed(void) {
    uint32_t now = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
    bool raw = (gpio_get_level(KEY_A_PIN) == 0);

    if (raw != s_key_prev) {
        s_key_prev = raw;
        s_key_change_ms = now;
        return false;
    }

    if ((now - s_key_change_ms) >= APP_KEY_DEBOUNCE_MS && raw != s_key_stable) {
        s_key_stable = raw;
        return s_key_stable;
    }

    return false;
}

static void can_stop(void) {
    if (s_can_started) {
        mcp2515_deinit(&s_can);
        s_can_started = false;
    }
}

static esp_err_t can_start(void) {
    esp_err_t err = mcp2515_init(&s_can, &s_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "MCP2515 init failed: %s", esp_err_to_name(err));
        return err;
    }
    s_can_started = true;

    esp_err_t selftest = mcp2515_selftest(&s_can);
    ESP_LOGI(TAG, "selftest: %s", esp_err_to_name(selftest));

    err = mcp2515_set_bitrate(&s_can, MCP2515_BITRATE_500K);
    if (err == ESP_OK) {
        err = mcp2515_set_mode(&s_can, MCP2515_MODE_NORMAL);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "CAN setup failed: %s", esp_err_to_name(err));
        can_stop();
        return err;
    }

    s_last_rx_ms = 0;
    s_rx_count = 0;
    s_last_rx_counter = 0;

    ESP_LOGI(TAG, "CAN ready: 500 kbit/s, NORMAL mode, node id=0x%03X",
             APP_NODE_ID);
    return ESP_OK;
}

static esp_err_t can_restart(void) {
    can_stop();
    vTaskDelay(pdMS_TO_TICKS(10));
    return can_start();
}

static void can_poll_rx(void) {
    while (1) {
        can_frame_t frame;

        esp_err_t err = mcp2515_receive(&s_can, &frame, 10);
        if (err == ESP_ERR_TIMEOUT) {
            break;
        }
        if (err == ESP_ERR_NOT_SUPPORTED) {
            continue;
        }
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "receive failed: %s", esp_err_to_name(err));
            break;
        }

        if (frame.id == APP_PARTNER_ID) {
            s_last_rx_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
            s_rx_count++;
            if (frame.dlc >= 4) {
                s_last_rx_counter = frame.data[3];
            }
        } else if (frame.id == APP_ACK_ID) {
            if (frame.dlc >= 4 &&
                frame.data[0] == 'A' && frame.data[1] == 'C' && frame.data[2] == 'K') {
                bool led_on = (frame.dlc >= 5) && (frame.data[4] != 0);
                ESP_LOGI(TAG, "ack seq=%u led=%s",
                         (unsigned)frame.data[3], led_on ? "ON" : "OFF");
            }
        }
    }
}

static esp_err_t can_send_heartbeat(uint8_t counter) {
    const can_frame_t tx = {
        .id = APP_NODE_ID,
        .dlc = 4,
        .data = {'L', 'I', 'L', counter},
    };

    return mcp2515_send(&s_can, &tx, 100);
}

static esp_err_t can_send_key_toggle(uint8_t seq) {
    const can_frame_t tx = {
        .id = APP_CMD_ID,
        .dlc = 4,
        .data = {'C', 'M', 'D', seq},
    };

    return mcp2515_send(&s_can, &tx, 100);
}

static void can_log_status(void) {
    mcp2515_status_t status = {0};
    esp_err_t err = mcp2515_get_status(&s_can, &status);
    uint32_t now = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
    bool link_up = (s_rx_count > 0) &&
                   ((now - s_last_rx_ms) < APP_LINK_TIMEOUT_MS);

    if (link_up) {
        ESP_LOGI(TAG,
                 "CAN: link up | rx=%lu last=%u age=%lu ms | EFLG=0x%02X CANINTF=0x%02X",
                 (unsigned long)s_rx_count,
                 (unsigned)s_last_rx_counter,
                 (unsigned long)(now - s_last_rx_ms),
                 (unsigned)status.eflg,
                 (unsigned)status.canintf);
    } else {
        ESP_LOGI(TAG, "CAN: no link | rx=%lu | EFLG=0x%02X CANINTF=0x%02X",
                 (unsigned long)s_rx_count,
                 (unsigned)status.eflg,
                 (unsigned)status.canintf);
    }

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "status read failed: %s", esp_err_to_name(err));
    }
    if (err == ESP_OK && status.bus_off) {
        ESP_LOGW(TAG, "bus off, restarting CAN");
        s_can_ready = false;
    }
}

static void can_task(void *arg) {
    (void)arg;

    TickType_t last_tx = xTaskGetTickCount();
    uint8_t tx_counter = 0;

    while (1) {
        bool key_press = key_a_just_pressed();

        if (!s_can_ready) {
            esp_err_t err = can_restart();
            s_can_ready = (err == ESP_OK);
            if (!s_can_ready) {
                ESP_LOGW(TAG, "CAN restart failed: %s", esp_err_to_name(err));
                vTaskDelay(pdMS_TO_TICKS(1000));
            }
            last_tx = xTaskGetTickCount();
            continue;
        }

        if (key_press) {
            uint8_t seq = s_cmd_seq++;
            esp_err_t err = can_send_key_toggle(seq);
            if (err == ESP_OK) {
                ESP_LOGI(TAG, "key A -> cmd seq=%u", (unsigned)seq);
            } else {
                ESP_LOGW(TAG, "key A send failed: %s", esp_err_to_name(err));
            }
        }

        TickType_t now = xTaskGetTickCount();

        if ((now - last_tx) >= pdMS_TO_TICKS(APP_TX_PERIOD_MS)) {
            last_tx = now;

            esp_err_t err = can_send_heartbeat(tx_counter++);
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "send failed: %s", esp_err_to_name(err));
                if (err == ESP_ERR_INVALID_STATE || err == ESP_FAIL) {
                    s_can_ready = false;
                }
            }

            can_log_status();
            if (!s_can_ready) {
                continue;
            }
        }

        can_poll_rx();
    }
}

void app_main(void) {
    ESP_LOGI(TAG, "starting CAN bridge node lilka (id=0x%03X, partner=0x%03X)",
             APP_NODE_ID, APP_PARTNER_ID);

    key_init();
    s_can_ready = (can_start() == ESP_OK);

    if (xTaskCreate(can_task, "can_task", 4096, NULL, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "cannot create can_task");
        abort();
    }
}
