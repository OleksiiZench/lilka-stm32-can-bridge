#include "mcp2515/mcp2515.h"

#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "mcp2515";

/* SPI instructions */
#define CMD_RESET 0xC0
#define CMD_READ  0x03

#define CMD_BIT_MODIFY 0x05
#define CMD_RTS_TXB0   0x81

/* Registers */
#define REG_CANSTAT 0x0E
#define REG_CANCTRL 0x0F

#define REG_CNF3     0x28
#define REG_CNF2     0x29
#define REG_CANINTF  0x2C
#define REG_TXB0CTRL 0x30
#define REG_TXB0SIDH 0x31
#define REG_RXB0CTRL 0x60
#define REG_RXB0SIDH 0x61
#define REG_RXB1CTRL 0x70

/* Bits */
#define CANINTF_RX0IF  (1 << 0)
#define CANINTF_TX0IF  (1 << 2)
#define TXBCTRL_TXREQ  (1 << 3)
#define TXBCTRL_TXERR  (1 << 4)
#define TXBCTRL_MLOA   (1 << 5)
#define TXBCTRL_ABTF   (1 << 6)
#define RXBCTRL_RXM_ANY 0x60
#define SIDL_IDE       (1 << 3)

/* Expected values after reset */
#define CANSTAT_OPMOD_MASK   0xE0
#define OPMOD_CONFIG         0x80
#define CANCTRL_RESET_VALUE  0x87

#define CMD_WRITE 0x02
#define REG_CNF1  0x2A

typedef struct {
    uint8_t cnf1, cnf2, cnf3;
} bit_timing_t;

static const bit_timing_t s_timing[2][MCP2515_BITRATE_COUNT] = {
    [MCP2515_OSC_8MHZ] = {
        [MCP2515_BITRATE_125K] = { 0x01, 0xB1, 0x85 },
        [MCP2515_BITRATE_250K] = { 0x00, 0xB1, 0x85 },
        [MCP2515_BITRATE_500K] = { 0x00, 0x90, 0x82 },
        [MCP2515_BITRATE_1M]   = { 0x00, 0x80, 0x80 },
    },
    [MCP2515_OSC_16MHZ] = {
        [MCP2515_BITRATE_125K] = { 0x03, 0xF0, 0x86 },
        [MCP2515_BITRATE_250K] = { 0x41, 0xF1, 0x85 },
        [MCP2515_BITRATE_500K] = { 0x00, 0xF0, 0x86 },
        [MCP2515_BITRATE_1M]   = { 0x00, 0xD0, 0x82 },
    },
};

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

static esp_err_t mcp2515_transfer(mcp2515_t *dev, const uint8_t *tx, uint8_t *rx, size_t len)
{
    spi_transaction_t t = {
        .length = len * 8,
        .tx_buffer = tx,
        .rx_buffer = rx,
    };
    return spi_device_polling_transmit(dev->spi, &t);
}

static esp_err_t mcp2515_bit_modify(mcp2515_t *dev, uint8_t addr, uint8_t mask, uint8_t data)
{
    uint8_t tx[4] = { CMD_BIT_MODIFY, addr, mask, data };
    return mcp2515_transfer(dev, tx, NULL, sizeof(tx));
}

static esp_err_t mcp2515_wait_reg(mcp2515_t *dev, uint8_t addr, uint8_t mask,
                                  uint8_t expected, uint32_t timeout_ms)
{
    const TickType_t start = xTaskGetTickCount();
    const TickType_t timeout = pdMS_TO_TICKS(timeout_ms);

    do {
        uint8_t value = 0;
        ESP_RETURN_ON_ERROR(mcp2515_read_reg(dev, addr, &value), TAG, "wait read");
        if ((value & mask) == expected) {
            return ESP_OK;
        }
        vTaskDelay(1);
    } while ((xTaskGetTickCount() - start) < timeout);

    return ESP_ERR_TIMEOUT;
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

    dev->osc = cfg->osc;

    ESP_RETURN_ON_ERROR(mcp2515_reset(dev), TAG, "reset failed");

    ESP_RETURN_ON_ERROR(mcp2515_write_reg(dev, REG_RXB0CTRL, RXBCTRL_RXM_ANY), TAG, "RXB0CTRL");
    ESP_RETURN_ON_ERROR(mcp2515_write_reg(dev, REG_RXB1CTRL, RXBCTRL_RXM_ANY), TAG, "RXB1CTRL");
    
    return ESP_OK;
}

esp_err_t mcp2515_read_reg(mcp2515_t *dev, uint8_t addr, uint8_t *value)
{
    uint8_t tx[3] = { CMD_READ, addr, 0x00 };
    uint8_t rx[3] = { 0 };

    ESP_RETURN_ON_ERROR(mcp2515_transfer(dev, tx, rx, sizeof(tx)), TAG, "read failed");

    *value = rx[2];
    return ESP_OK;
}

esp_err_t mcp2515_probe(mcp2515_t *dev)
{
    uint8_t canstat = 0, canctrl = 0;

    ESP_RETURN_ON_ERROR(mcp2515_read_reg(dev, REG_CANSTAT, &canstat), TAG, "read CANSTAT");
    ESP_RETURN_ON_ERROR(mcp2515_read_reg(dev, REG_CANCTRL, &canctrl), TAG, "read CANCTRL");

    ESP_LOGD(TAG, "CANSTAT=0x%02X CANCTRL=0x%02X", canstat, canctrl);

    const bool mode_matches = (canstat & CANSTAT_OPMOD_MASK) == (canctrl & CANSTAT_OPMOD_MASK);
    const bool unused_bits_zero = (canstat & 0x11) == 0;
    const bool clock_bits_ok = (canctrl & 0x07) == 0x07;

    if (!mode_matches || !unused_bits_zero || !clock_bits_ok) {
        return ESP_ERR_NOT_FOUND;
    }
    return ESP_OK;
}

esp_err_t mcp2515_write_reg(mcp2515_t *dev, uint8_t addr, uint8_t value)
{
    uint8_t tx[3] = { CMD_WRITE, addr, value };

    ESP_RETURN_ON_ERROR(mcp2515_transfer(dev, tx, 0, sizeof(tx)), TAG, "write failed");

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

esp_err_t mcp2515_set_mode(mcp2515_t *dev, mcp2515_mode_t mode)
{
    ESP_RETURN_ON_ERROR(mcp2515_bit_modify(dev, REG_CANCTRL, CANSTAT_OPMOD_MASK, mode),
                        TAG, "set REQOP");

    return mcp2515_wait_reg(dev, REG_CANSTAT, CANSTAT_OPMOD_MASK, mode, 50);
}

esp_err_t mcp2515_set_bitrate(mcp2515_t *dev, mcp2515_bitrate_t bitrate)
{
    ESP_RETURN_ON_FALSE(bitrate < MCP2515_BITRATE_COUNT, ESP_ERR_INVALID_ARG, TAG, "bad bitrate");

    ESP_RETURN_ON_ERROR(mcp2515_set_mode(dev, MCP2515_MODE_CONFIG), TAG, "enter config");

    const bit_timing_t *t = &s_timing[dev->osc][bitrate];
    ESP_RETURN_ON_ERROR(mcp2515_write_reg(dev, REG_CNF1, t->cnf1), TAG, "CNF1");
    ESP_RETURN_ON_ERROR(mcp2515_write_reg(dev, REG_CNF2, t->cnf2), TAG, "CNF2");
    ESP_RETURN_ON_ERROR(mcp2515_write_reg(dev, REG_CNF3, t->cnf3), TAG, "CNF3");
    
    return ESP_OK;
}

esp_err_t mcp2515_send(mcp2515_t *dev, const can_frame_t *frame, uint32_t timeout_ms)
{
    ESP_RETURN_ON_FALSE(frame->dlc <= 8 && frame->id <= 0x7FF, ESP_ERR_INVALID_ARG, TAG, "bad frame");

    uint8_t tx[2 + 5 + 8] = { CMD_WRITE, REG_TXB0SIDH };
    tx[2] = (uint8_t)(frame->id >> 3);          /* SIDH */
    tx[3] = (uint8_t)((frame->id & 0x07) << 5); /* SIDL */
    tx[4] = 0;                                  /* EID8 */
    tx[5] = 0;                                  /* EID0 */
    tx[6] = frame->dlc;                         /* DLC */
    memcpy(&tx[7], frame->data, frame->dlc);
    ESP_RETURN_ON_ERROR(mcp2515_transfer(dev, tx, NULL, 7 + frame->dlc), TAG, "load TXB0");

    uint8_t rts = CMD_RTS_TXB0;
    ESP_RETURN_ON_ERROR(mcp2515_transfer(dev, &rts, NULL, 1), TAG, "RTS");

    /* TXREQ скидається, коли чип завершив (або облишив) відправку */
    esp_err_t err = mcp2515_wait_reg(dev, REG_TXB0CTRL, TXBCTRL_TXREQ, 0, timeout_ms);
    if (err == ESP_ERR_TIMEOUT) {
        /* наприклад, ніхто не підтвердив фрейм (ACK) на реальній шині: скасовуємо */
        mcp2515_bit_modify(dev, REG_TXB0CTRL, TXBCTRL_TXREQ, 0);
        return err;
    }
    ESP_RETURN_ON_ERROR(err, TAG, "wait TXREQ");

    uint8_t ctrl = 0;
    ESP_RETURN_ON_ERROR(mcp2515_read_reg(dev, REG_TXB0CTRL, &ctrl), TAG, "read TXB0CTRL");
    ESP_RETURN_ON_ERROR(mcp2515_bit_modify(dev, REG_CANINTF, CANINTF_TX0IF, 0), TAG, "clear TX0IF");

    if (ctrl & (TXBCTRL_TXERR | TXBCTRL_MLOA | TXBCTRL_ABTF)) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t mcp2515_receive(mcp2515_t *dev, can_frame_t *frame, uint32_t timeout_ms)
{
    esp_err_t err = mcp2515_wait_reg(dev, REG_CANINTF, CANINTF_RX0IF, CANINTF_RX0IF, timeout_ms);
    if (err != ESP_OK) {
        return err;
    }

    uint8_t tx[2 + 13] = { CMD_READ, REG_RXB0SIDH };
    uint8_t rx[sizeof(tx)] = { 0 };
    ESP_RETURN_ON_ERROR(mcp2515_transfer(dev, tx, rx, sizeof(tx)), TAG, "read RXB0");

    const uint8_t *r = &rx[2];

    ESP_RETURN_ON_ERROR(mcp2515_bit_modify(dev, REG_CANINTF, CANINTF_RX0IF, 0), TAG, "clear RX0IF");

    if (r[1] & SIDL_IDE) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    frame->id = ((uint16_t)r[0] << 3) | (r[1] >> 5);
    frame->dlc = r[4] & 0x0F;
    if (frame->dlc > 8) {
        frame->dlc = 8;
    }
    memcpy(frame->data, &r[5], 8);
    return ESP_OK;
}
