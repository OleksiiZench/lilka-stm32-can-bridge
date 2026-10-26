#include "mcp2515/mcp2515.h"

#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "mcp2515";

/* SPI instructions */
#define CMD_RESET 0xC0
#define CMD_READ 0x03
#define CMD_WRITE 0x02
#define CMD_BIT_MODIFY 0x05
#define CMD_RTS_TXB0 0x81

/* Registers */
#define REG_CANSTAT 0x0E
#define REG_CANCTRL 0x0F

#define REG_CNF3 0x28
#define REG_CNF2 0x29
#define REG_CNF1 0x2A
#define REG_CANINTF 0x2C
#define REG_EFLG 0x2D
#define REG_TXB0CTRL 0x30
#define REG_TXB0SIDH 0x31
#define REG_RXB0CTRL 0x60
#define REG_RXB0SIDH 0x61
#define REG_RXB1CTRL 0x70

/* CANINTF bits */
#define CANINTF_RX0IF (1 << 0)
#define CANINTF_TX0IF (1 << 2)
#define CANINTF_ERRIF (1 << 5)

/* EFLG bits */
#define EFLG_TXBO (1 << 5)
#define EFLG_RX0OVR (1 << 6)

/* TXBnCTRL bits */
#define TXBCTRL_TXREQ (1 << 3)
#define TXBCTRL_TXERR (1 << 4)
#define TXBCTRL_MLOA (1 << 5)
#define TXBCTRL_ABTF (1 << 6)

/* RXBnSIDL / RXBnDLC bits */
#define SIDL_IDE (1 << 3)
#define DLC_RTR (1 << 6)
#define DLC_MASK 0x0F

/* RXBnCTRL.RXM[1:0] = 11 -> masks/filters off, accept any message */
#define RXBCTRL_RXM_ANY 0x60

/* Expected values after reset */
#define CANSTAT_OPMOD_MASK 0xE0
#define OPMOD_CONFIG 0x80

/* Marker for a bit rate that cannot be reached with the given oscillator */
#define BIT_TIMING_UNSUPPORTED 0xFF

typedef struct {
    uint8_t cnf1, cnf2, cnf3;
} bit_timing_t;

/* TQ = 2 * (BRP + 1) / Fosc, N = 1 + PRSEG + PHSEG1 + PHSEG2 time quanta per bit */
static const bit_timing_t s_timing[2][MCP2515_BITRATE_COUNT] = {
    [MCP2515_OSC_8MHZ] =
        {
            [MCP2515_BITRATE_125K] = {0x01, 0xB1, 0x85}, /* 16 TQ, sample point 62.5% */
            [MCP2515_BITRATE_250K] = {0x00, 0xB1, 0x85}, /* 16 TQ, sample point 62.5% */
            [MCP2515_BITRATE_500K] = {0x00, 0x90, 0x82}, /*  8 TQ, sample point 62.5% */
            /* 1 Mbit/s would need 4 TQ per bit: below the 8 TQ required by CAN and
               it would force PS2 = 1 TQ, below the 2 TQ required by the MCP2515. */
            [MCP2515_BITRATE_1M] = {BIT_TIMING_UNSUPPORTED, 0x00, 0x00},
        },
    [MCP2515_OSC_16MHZ] =
        {
            [MCP2515_BITRATE_125K] = {0x03, 0xF0, 0x86}, /* 16 TQ, sample point 56.3% */
            [MCP2515_BITRATE_250K] = {0x41, 0xF1, 0x85}, /* 16 TQ, sample point 62.5% */
            [MCP2515_BITRATE_500K] = {0x00, 0xF0, 0x86}, /* 16 TQ, sample point 56.3% */
            [MCP2515_BITRATE_1M] = {0x00, 0xD0, 0x82},   /*  8 TQ, sample point 62.5% */
        },
};

static esp_err_t mcp2515_wait_reg(mcp2515_t *dev, uint8_t addr, uint8_t mask, uint8_t expected,
                                  uint32_t timeout_ms) {
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

static esp_err_t mcp2515_reset(mcp2515_t *dev) {
    uint8_t cmd = CMD_RESET;
    spi_transaction_t t = {
        .length = 8,
        .tx_buffer = &cmd,
    };
    ESP_RETURN_ON_ERROR(spi_device_polling_transmit(dev->spi, &t), TAG, "reset failed");

    /* The controller needs a moment to finish the internal reset: wait until it
       reports the configuration mode instead of guessing a fixed delay. */
    return mcp2515_wait_reg(dev, REG_CANSTAT, CANSTAT_OPMOD_MASK, OPMOD_CONFIG, 100);
}

static esp_err_t mcp2515_transfer(mcp2515_t *dev, const uint8_t *tx, uint8_t *rx, size_t len) {
    spi_transaction_t t = {
        .length = len * 8,
        .tx_buffer = tx,
        .rx_buffer = rx,
    };
    return spi_device_polling_transmit(dev->spi, &t);
}

static esp_err_t mcp2515_bit_modify(mcp2515_t *dev, uint8_t addr, uint8_t mask, uint8_t data) {
    uint8_t tx[4] = {CMD_BIT_MODIFY, addr, mask, data};
    return mcp2515_transfer(dev, tx, NULL, sizeof(tx));
}

esp_err_t mcp2515_init(mcp2515_t *dev, const mcp2515_config_t *cfg) {
    ESP_RETURN_ON_FALSE(cfg->osc < MCP2515_OSC_COUNT, ESP_ERR_INVALID_ARG, TAG, "bad osc");

    dev->spi = NULL;
    dev->host = cfg->host;
    dev->osc = cfg->osc;

    spi_bus_config_t bus = {
        .mosi_io_num = cfg->mosi,
        .miso_io_num = cfg->miso,
        .sclk_io_num = cfg->sclk,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 16,
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(cfg->host, &bus, SPI_DMA_CH_AUTO), TAG,
                        "bus init failed");

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = cfg->clock_hz,
        .mode = 0,
        .spics_io_num = cfg->cs,
        .queue_size = 1,
    };
    esp_err_t err = spi_bus_add_device(cfg->host, &devcfg, &dev->spi);
    if (err != ESP_OK) {
        spi_bus_free(cfg->host);
        ESP_RETURN_ON_ERROR(err, TAG, "add device failed");
    }

    err = mcp2515_reset(dev);
    if (err != ESP_OK) {
        mcp2515_deinit(dev);
        ESP_RETURN_ON_ERROR(err, TAG, "reset failed");
    }

    err = mcp2515_write_reg(dev, REG_RXB0CTRL, RXBCTRL_RXM_ANY);
    if (err == ESP_OK) {
        err = mcp2515_write_reg(dev, REG_RXB1CTRL, RXBCTRL_RXM_ANY);
    }
    if (err != ESP_OK) {
        mcp2515_deinit(dev);
        ESP_RETURN_ON_ERROR(err, TAG, "RXBCTRL");
    }

    return ESP_OK;
}

void mcp2515_deinit(mcp2515_t *dev) {
    if (dev->spi != NULL) {
        spi_bus_remove_device(dev->spi);
        dev->spi = NULL;
    }
    if (dev->host != SPI_HOST_MAX) {
        spi_bus_free(dev->host);
        dev->host = SPI_HOST_MAX;
    }
}

esp_err_t mcp2515_read_reg(mcp2515_t *dev, uint8_t addr, uint8_t *value) {
    uint8_t tx[3] = {CMD_READ, addr, 0x00};
    uint8_t rx[3] = {0};

    ESP_RETURN_ON_ERROR(mcp2515_transfer(dev, tx, rx, sizeof(tx)), TAG, "read failed");

    *value = rx[2];
    return ESP_OK;
}

esp_err_t mcp2515_probe(mcp2515_t *dev) {
    uint8_t canstat = 0, canctrl = 0;

    ESP_RETURN_ON_ERROR(mcp2515_read_reg(dev, REG_CANSTAT, &canstat), TAG, "read CANSTAT");
    ESP_RETURN_ON_ERROR(mcp2515_read_reg(dev, REG_CANCTRL, &canctrl), TAG, "read CANCTRL");

    ESP_LOGD(TAG, "CANSTAT=0x%02X CANCTRL=0x%02X", canstat, canctrl);

    /* CANSTAT holds the operation mode (OPMOD[2:0]) and the interrupt code
       (ICOD[2:0]); the interrupt flags themselves live in CANINTF/EFLG. */
    const bool mode_matches = (canstat & CANSTAT_OPMOD_MASK) == (canctrl & CANSTAT_OPMOD_MASK);
    const bool clock_bits_ok = (canctrl & 0x07) == 0x07;

    if (!mode_matches || !clock_bits_ok) {
        return ESP_ERR_NOT_FOUND;
    }
    return ESP_OK;
}

esp_err_t mcp2515_get_status(mcp2515_t *dev, mcp2515_status_t *status) {
    ESP_RETURN_ON_FALSE(status != NULL, ESP_ERR_INVALID_ARG, TAG, "null status");

    memset(status, 0, sizeof(*status));

    ESP_RETURN_ON_ERROR(mcp2515_read_reg(dev, REG_CANINTF, &status->canintf), TAG, "read CANINTF");
    ESP_RETURN_ON_ERROR(mcp2515_read_reg(dev, REG_EFLG, &status->eflg), TAG, "read EFLG");

    status->bus_off = (status->eflg & EFLG_TXBO) != 0;
    status->rx_overflow = (status->eflg & EFLG_RX0OVR) != 0;
    status->error = (status->canintf & CANINTF_ERRIF) != 0;

    return ESP_OK;
}

esp_err_t mcp2515_write_reg(mcp2515_t *dev, uint8_t addr, uint8_t value) {
    uint8_t tx[3] = {CMD_WRITE, addr, value};

    ESP_RETURN_ON_ERROR(mcp2515_transfer(dev, tx, 0, sizeof(tx)), TAG, "write failed");

    return ESP_OK;
}

esp_err_t mcp2515_selftest(mcp2515_t *dev) {
    static const uint8_t patterns[] = {0x55, 0xAA};

    uint8_t original = 0;
    ESP_RETURN_ON_ERROR(mcp2515_read_reg(dev, REG_CNF1, &original), TAG, "read original");

    esp_err_t result = ESP_OK;

    for (size_t i = 0; i < sizeof(patterns); i++) {
        uint8_t readback = 0;

        esp_err_t err = mcp2515_write_reg(dev, REG_CNF1, patterns[i]);
        if (err != ESP_OK) {
            result = err;
            break;
        }

        err = mcp2515_read_reg(dev, REG_CNF1, &readback);
        if (err != ESP_OK) {
            result = err;
            break;
        }

        if (readback != patterns[i]) {
            ESP_LOGE(TAG, "CNF1 mismatch: wrote 0x%02X, read 0x%02X", patterns[i], readback);
            result = ESP_ERR_INVALID_RESPONSE;
            break;
        }
    }

    /* CNF1 holds the bit timing: it has to be restored even when the test failed,
       otherwise the controller keeps the garbage pattern as its timing. */
    esp_err_t restore = mcp2515_write_reg(dev, REG_CNF1, original);
    if (restore != ESP_OK) {
        ESP_LOGE(TAG, "restore CNF1 failed: %s", esp_err_to_name(restore));
        return restore;
    }

    return result;
}

esp_err_t mcp2515_set_mode(mcp2515_t *dev, mcp2515_mode_t mode) {
    ESP_RETURN_ON_ERROR(mcp2515_bit_modify(dev, REG_CANCTRL, CANSTAT_OPMOD_MASK, mode), TAG,
                        "set REQOP");

    return mcp2515_wait_reg(dev, REG_CANSTAT, CANSTAT_OPMOD_MASK, mode, 50);
}

esp_err_t mcp2515_set_bitrate(mcp2515_t *dev, mcp2515_bitrate_t bitrate) {
    ESP_RETURN_ON_FALSE(bitrate < MCP2515_BITRATE_COUNT, ESP_ERR_INVALID_ARG, TAG, "bad bitrate");
    ESP_RETURN_ON_FALSE(dev->osc < MCP2515_OSC_COUNT, ESP_ERR_INVALID_ARG, TAG, "bad osc");

    const bit_timing_t *t = &s_timing[dev->osc][bitrate];
    if (t->cnf1 == BIT_TIMING_UNSUPPORTED) {
        ESP_LOGE(TAG, "bitrate not reachable with the configured oscillator");
        return ESP_ERR_NOT_SUPPORTED;
    }

    /* Remember the requested mode: the caller should not have to restore it. */
    uint8_t canctrl = 0;
    ESP_RETURN_ON_ERROR(mcp2515_read_reg(dev, REG_CANCTRL, &canctrl), TAG, "read CANCTRL");
    const mcp2515_mode_t prev_mode = (mcp2515_mode_t)(canctrl & CANSTAT_OPMOD_MASK);

    ESP_RETURN_ON_ERROR(mcp2515_set_mode(dev, MCP2515_MODE_CONFIG), TAG, "enter config");

    esp_err_t err = mcp2515_write_reg(dev, REG_CNF1, t->cnf1);
    if (err == ESP_OK) {
        err = mcp2515_write_reg(dev, REG_CNF2, t->cnf2);
    }
    if (err == ESP_OK) {
        err = mcp2515_write_reg(dev, REG_CNF3, t->cnf3);
    }

    esp_err_t restore = mcp2515_set_mode(dev, prev_mode);

    ESP_RETURN_ON_ERROR(err, TAG, "write CNF");
    ESP_RETURN_ON_ERROR(restore, TAG, "restore mode");

    return ESP_OK;
}

esp_err_t mcp2515_send(mcp2515_t *dev, const can_frame_t *frame, uint32_t timeout_ms) {
    ESP_RETURN_ON_FALSE(frame->dlc <= 8 && frame->id <= 0x7FF, ESP_ERR_INVALID_ARG, TAG,
                        "bad frame");

    /* The buffer has to be free, otherwise the pending frame would be overwritten. */
    uint8_t ctrl = 0;
    ESP_RETURN_ON_ERROR(mcp2515_read_reg(dev, REG_TXB0CTRL, &ctrl), TAG, "read TXB0CTRL");
    ESP_RETURN_ON_FALSE((ctrl & TXBCTRL_TXREQ) == 0, ESP_ERR_INVALID_STATE, TAG, "TXB0 busy");

    uint8_t tx[2 + 5 + 8] = {CMD_WRITE, REG_TXB0SIDH};
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
        esp_err_t abort_err = mcp2515_bit_modify(dev, REG_TXB0CTRL, TXBCTRL_TXREQ, 0);
        if (abort_err != ESP_OK) {
            ESP_LOGE(TAG, "abort TXB0 failed: %s", esp_err_to_name(abort_err));
        }
        return err;
    }
    ESP_RETURN_ON_ERROR(err, TAG, "wait TXREQ");

    /* The error flags have to be evaluated before the interrupt flag is cleared,
       otherwise a failed transmission is reported as a successful one. */
    ESP_RETURN_ON_ERROR(mcp2515_read_reg(dev, REG_TXB0CTRL, &ctrl), TAG, "read TXB0CTRL");
    ESP_RETURN_ON_ERROR(mcp2515_bit_modify(dev, REG_CANINTF, CANINTF_TX0IF, 0), TAG, "clear TX0IF");

    if (ctrl & (TXBCTRL_TXERR | TXBCTRL_MLOA | TXBCTRL_ABTF)) {
        ESP_LOGE(TAG, "TX failed: TXB0CTRL=0x%02X", ctrl);
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t mcp2515_receive(mcp2515_t *dev, can_frame_t *frame, uint32_t timeout_ms) {
    ESP_RETURN_ON_ERROR(
        mcp2515_wait_reg(dev, REG_CANINTF, CANINTF_RX0IF, CANINTF_RX0IF, timeout_ms), TAG,
        "wait RX0IF");

    /* RXB0SIDH .. RXB0D7: 13 consecutive registers */
    uint8_t tx[2 + 13] = {CMD_READ, REG_RXB0SIDH};
    uint8_t rx[sizeof(tx)] = {0};
    ESP_RETURN_ON_ERROR(mcp2515_transfer(dev, tx, rx, sizeof(tx)), TAG, "read RXB0");

    const uint8_t *r = &rx[2];

    /* Decode before clearing RX0IF, so that the frame is not silently dropped. */
    const bool extended = (r[1] & SIDL_IDE) != 0;
    const bool remote = (r[4] & DLC_RTR) != 0;

    memset(frame->data, 0, sizeof(frame->data));
    frame->id = ((uint16_t)r[0] << 3) | (r[1] >> 5);
    frame->dlc = r[4] & DLC_MASK;
    if (frame->dlc > 8) {
        frame->dlc = 8;
    }
    memcpy(frame->data, &r[5], 8);

    /* The frame is consumed even when it cannot be decoded, otherwise it stays in
       the buffer forever and the receiver spins on the same frame. */
    ESP_RETURN_ON_ERROR(mcp2515_bit_modify(dev, REG_CANINTF, CANINTF_RX0IF, 0), TAG, "clear RX0IF");

    if (extended) {
        ESP_LOGW(TAG, "extended frame dropped (id=0x%03X)", frame->id);
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (remote) {
        ESP_LOGW(TAG, "remote frame dropped (id=0x%03X)", frame->id);
        return ESP_ERR_NOT_SUPPORTED;
    }

    return ESP_OK;
}
