#include "mcp2515/mcp2515.h"

#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "mcp2515";

/* SPI instructions */
#define CMD_RESET 0xC0
#define CMD_READ  0x03

/* Registers */
#define REG_CANSTAT 0x0E
#define REG_CANCTRL 0x0F

/* Expected values after reset */
#define CANSTAT_OPMOD_MASK   0xE0
#define OPMOD_CONFIG         0x80
#define CANCTRL_RESET_VALUE  0x87

#define CMD_WRITE 0x02
#define REG_CNF1  0x2A

static esp_err_t mcp2515_reset(mcp2515_t *dev)
{
    uint8_t cmd = CMD_RESET;
    spi_transaction_t t = {
        .length = 8,
        .tx_buffer = &cmd,
    };
    ESP_RETURN_ON_ERROR(spi_device_polling_transmit(dev->spi, &t), TAG, "reset failed");

    vTaskDelay(pdMS_TO_TICKS(10));
    return ESP_OK;
}

esp_err_t mcp2515_init(mcp2515_t *dev, const mcp2515_config_t *cfg)
{
    spi_bus_config_t bus = {
        .mosi_io_num = cfg->mosi,
        .miso_io_num = cfg->miso,
        .sclk_io_num = cfg->sclk,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 16,
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(cfg->host, &bus, SPI_DMA_CH_AUTO), TAG, "bus init failed");

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = cfg->clock_hz,
        .mode = 0,
        .spics_io_num = cfg->cs,
        .queue_size = 1,
    };
    ESP_RETURN_ON_ERROR(spi_bus_add_device(cfg->host, &devcfg, &dev->spi), TAG, "add device failed");

    return mcp2515_reset(dev);
}

esp_err_t mcp2515_read_reg(mcp2515_t *dev, uint8_t addr, uint8_t *value)
{
    uint8_t tx[3] = { CMD_READ, addr, 0x00 };
    uint8_t rx[3] = { 0 };

    spi_transaction_t t = {
        .length = 24,
        .tx_buffer = tx,
        .rx_buffer = rx,
    };
    ESP_RETURN_ON_ERROR(spi_device_polling_transmit(dev->spi, &t), TAG, "read failed");

    *value = rx[2];
    return ESP_OK;
}

esp_err_t mcp2515_probe(mcp2515_t *dev)
{
    uint8_t canstat = 0, canctrl = 0;

    ESP_RETURN_ON_ERROR(mcp2515_read_reg(dev, REG_CANSTAT, &canstat), TAG, "read CANSTAT");
    ESP_RETURN_ON_ERROR(mcp2515_read_reg(dev, REG_CANCTRL, &canctrl), TAG, "read CANCTRL");

    ESP_LOGD(TAG, "CANSTAT=0x%02X CANCTRL=0x%02X", canstat, canctrl);

    if ((canstat & CANSTAT_OPMOD_MASK) != OPMOD_CONFIG || canctrl != CANCTRL_RESET_VALUE) {
        return ESP_ERR_NOT_FOUND;
    }
    return ESP_OK;
}

esp_err_t mcp2515_write_reg(mcp2515_t *dev, uint8_t addr, uint8_t value)
{
    uint8_t tx[3] = { CMD_WRITE, addr, value };

    spi_transaction_t t = {
        .length = 24,
        .tx_buffer = tx,
    };
    ESP_RETURN_ON_ERROR(spi_device_polling_transmit(dev->spi, &t), TAG, "write failed");
    return ESP_OK;
}

esp_err_t mcp2515_selftest(mcp2515_t *dev)
{
    static const uint8_t patterns[] = { 0x55, 0xAA };

    uint8_t original = 0;
    ESP_RETURN_ON_ERROR(mcp2515_read_reg(dev, REG_CNF1, &original), TAG, "read original");

    esp_err_t result = ESP_OK;

    for (size_t i = 0; i < sizeof(patterns); i++) {
        uint8_t readback = 0;

        ESP_RETURN_ON_ERROR(mcp2515_write_reg(dev, REG_CNF1, patterns[i]), TAG, "write pattern");
        ESP_RETURN_ON_ERROR(mcp2515_read_reg(dev, REG_CNF1, &readback), TAG, "read pattern");

        if (readback != patterns[i]) {
            ESP_LOGE(TAG, "CNF1 mismatch: wrote 0x%02X, read 0x%02X", patterns[i], readback);
            result = ESP_ERR_INVALID_RESPONSE;
            break;
        }
    }

    ESP_RETURN_ON_ERROR(mcp2515_write_reg(dev, REG_CNF1, original), TAG, "restore");
    return result;
}
