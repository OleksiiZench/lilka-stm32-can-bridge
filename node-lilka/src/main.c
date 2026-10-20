#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "board_pins.h"
#include "mcp2515/mcp2515.h"

static const char *TAG = "app";

static mcp2515_t s_can;

static void can_monitor_task(void *arg)
{
    while (1) 
    {
        esp_err_t err = mcp2515_probe(&s_can);

        if (err == ESP_OK)
        {
            ESP_LOGI(TAG, "MCP2515 відповідає");
        }
        else
        {
            ESP_LOGW(TAG, "MCP2515 НЕ відповідає (%s)", esp_err_to_name(err));
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void app_main(void)
{
    const mcp2515_config_t cfg = {
        .host = SPI3_HOST,
        .mosi = CAN_PIN_MOSI,
        .miso = CAN_PIN_MISO,
        .sclk = CAN_PIN_SCLK,
        .cs = CAN_PIN_CS,
        .clock_hz = 1 * 1000 * 1000,
    };

    ESP_ERROR_CHECK(mcp2515_init(&s_can, &cfg));
    xTaskCreate(can_monitor_task, "can_monitor", 4096, NULL, 5, NULL);
}
