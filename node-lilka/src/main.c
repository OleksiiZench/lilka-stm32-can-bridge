#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "board_pins.h"
#include "mcp2515/mcp2515.h"

static const char *TAG = "app";

static mcp2515_t s_can;

static esp_err_t loopback_roundtrip(uint8_t counter) {
    const can_frame_t sent = {
        .id = 0x123,
        .dlc = 8,
        .data = {0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x00, 0x00, counter},
    };
    can_frame_t received = {0};

    esp_err_t err = mcp2515_send(&s_can, &sent, 100);
    if (err != ESP_OK) {
        return err;
    }

    err = mcp2515_receive(&s_can, &received, 100);
    if (err != ESP_OK) {
        return err;
    }

    if (received.id != sent.id || received.dlc != sent.dlc ||
        memcmp(received.data, sent.data, sent.dlc) != 0) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    return ESP_OK;
}

static void can_monitor_task(void *arg) {
    uint8_t counter = 0;

    while (1) {
        esp_err_t probe = mcp2515_probe(&s_can);
        esp_err_t loop = loopback_roundtrip(counter++);

        ESP_LOGI(TAG, "probe: %s | loopback: %s (cnt=%u)", probe == ESP_OK ? "OK" : "FAIL",
                 esp_err_to_name(loop), counter - 1);

        /* Bus errors and lost frames are invisible in the interrupt flags of the
           receive path, they have to be read from EFLG explicitly. */
        mcp2515_status_t status = {0};
        if (mcp2515_get_status(&s_can, &status) == ESP_OK &&
            (status.bus_off || status.rx_overflow || status.error)) {
            ESP_LOGW(TAG,
                     "CAN status: bus_off=%d rx_overflow=%d error=%d "
                     "(CANINTF=0x%02X EFLG=0x%02X)",
                     status.bus_off, status.rx_overflow, status.error, status.canintf, status.eflg);
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void app_main(void) {
    const mcp2515_config_t cfg = {
        .host = SPI3_HOST,
        .mosi = CAN_PIN_MOSI,
        .miso = CAN_PIN_MISO,
        .sclk = CAN_PIN_SCLK,
        .cs = CAN_PIN_CS,
        .clock_hz = 1 * 1000 * 1000,
        .osc = MCP2515_OSC_8MHZ,
    };

    ESP_ERROR_CHECK(mcp2515_init(&s_can, &cfg));

    esp_err_t st = mcp2515_selftest(&s_can);
    ESP_LOGI(TAG, "startup selftest: %s", esp_err_to_name(st));

    esp_err_t err = mcp2515_set_bitrate(&s_can, MCP2515_BITRATE_500K);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "failed to set bitrate: %s; CAN monitor not started", esp_err_to_name(err));
        return;
    }

    /* Loopback keeps the traffic off the bus: the real bridge mode is NORMAL. */
    err = mcp2515_set_mode(&s_can, MCP2515_MODE_LOOPBACK);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "failed to enter loopback mode: %s; CAN monitor not started",
                 esp_err_to_name(err));
        return;
    }

    if (xTaskCreate(can_monitor_task, "can_monitor", 4096, NULL, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "cannot create can_monitor task");
        abort();
    }
}
