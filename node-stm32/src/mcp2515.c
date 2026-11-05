#include "mcp2515.h"

#include <string.h>

#define CMD_RESET           0xC0
#define CMD_READ            0x03
#define CMD_WRITE           0x02
#define CMD_BIT_MODIFY      0x05
#define CMD_RTS_TXB0        0x81

#define REG_CANSTAT         0x0E
#define REG_CANCTRL         0x0F
#define REG_CNF3            0x28
#define REG_CNF2            0x29
#define REG_CNF1            0x2A
#define REG_CANINTF         0x2C
#define REG_EFLG            0x2D
#define REG_TXB0CTRL        0x30
#define REG_TXB0SIDH        0x31
#define REG_RXB0CTRL        0x60
#define REG_RXB0SIDH        0x61
#define REG_RXB1CTRL        0x70

#define CANINTF_RX0IF       (1 << 0)
#define CANINTF_TX0IF       (1 << 2)
#define CANINTF_ERRIF       (1 << 5)

#define EFLG_TXBO           (1 << 5)
#define EFLG_RX0OVR         (1 << 6)
#define EFLG_RX1OVR         (1 << 7)

#define TXBCTRL_TXREQ       (1 << 3)
#define TXBCTRL_TXERR       (1 << 4)
#define TXBCTRL_MLOA        (1 << 5)
#define TXBCTRL_ABTF        (1 << 6)

#define SIDL_IDE            (1 << 3)
#define DLC_RTR             (1 << 6)
#define DLC_MASK            0x0F

#define RXBCTRL_RXM_ANY     0x60

#define TXB0_CMD            0
#define TXB0_ADDR           1
#define TXB0_SIDH           2
#define TXB0_SIDL           3
#define TXB0_EID8           4
#define TXB0_EID0           5
#define TXB0_DLC            6
#define TXB0_DATA           7

#define RXB0_CMD            0
#define RXB0_ADDR           1
#define RXB0_DATA           2
#define RXB0_FRAME_LEN      13

#define CANSTAT_OPMOD_MASK  0xE0
#define OPMOD_CONFIG        0x80

#define BIT_TIMING_UNSUPPORTED 0xFF

#define MCP2515_RETURN_ON_ERROR(expr)      \
    do {                                   \
        mcp2515_err_t _err = (expr);       \
        if (_err != MCP2515_OK) {          \
            return _err;                   \
        }                                  \
    } while (0)

typedef struct {
    uint8_t cnf1;
    uint8_t cnf2;
    uint8_t cnf3;
} bit_timing_t;

static const bit_timing_t s_timing[MCP2515_OSC_COUNT][MCP2515_BITRATE_COUNT] = {
    [MCP2515_OSC_8MHZ] = {
        [MCP2515_BITRATE_125K] = {0x01, 0xB1, 0x85},
        [MCP2515_BITRATE_250K] = {0x00, 0xB1, 0x85},
        [MCP2515_BITRATE_500K] = {0x00, 0x90, 0x82},
        [MCP2515_BITRATE_1M]   = {BIT_TIMING_UNSUPPORTED, 0x00, 0x00},
    },
    [MCP2515_OSC_16MHZ] = {
        [MCP2515_BITRATE_125K] = {0x03, 0xF0, 0x86},
        [MCP2515_BITRATE_250K] = {0x41, 0xF1, 0x85},
        [MCP2515_BITRATE_500K] = {0x00, 0xF0, 0x86},
        [MCP2515_BITRATE_1M]   = {0x00, 0xD0, 0x82},
    },
};

static mcp2515_err_t spi_transfer(mcp2515_t *dev, const uint8_t *tx, uint8_t *rx,
                                  size_t len) {
    HAL_StatusTypeDef st;

    if (dev == NULL || tx == NULL || len == 0 || len > UINT16_MAX) {
        return MCP2515_ERR_INVALID_ARG;
    }

    HAL_GPIO_WritePin(dev->cs_port, dev->cs_pin, GPIO_PIN_RESET);

    if (rx != NULL) {
        st = HAL_SPI_TransmitReceive(dev->spi, (uint8_t *)tx, rx, (uint16_t)len,
                                     dev->spi_timeout_ms);
    } else {
        st = HAL_SPI_Transmit(dev->spi, (uint8_t *)tx, (uint16_t)len,
                              dev->spi_timeout_ms);
    }

    HAL_GPIO_WritePin(dev->cs_port, dev->cs_pin, GPIO_PIN_SET);

    if (st == HAL_TIMEOUT) {
        return MCP2515_ERR_TIMEOUT;
    }
    if (st != HAL_OK) {
        return MCP2515_ERR_IO;
    }
    return MCP2515_OK;
}

mcp2515_err_t mcp2515_read_reg(mcp2515_t *dev, uint8_t addr, uint8_t *value) {
    uint8_t tx[3] = {CMD_READ, addr, 0x00};
    uint8_t rx[3] = {0};

    if (dev == NULL || value == NULL) {
        return MCP2515_ERR_INVALID_ARG;
    }

    MCP2515_RETURN_ON_ERROR(spi_transfer(dev, tx, rx, sizeof(tx)));
    *value = rx[2];
    return MCP2515_OK;
}

mcp2515_err_t mcp2515_write_reg(mcp2515_t *dev, uint8_t addr, uint8_t value) {
    uint8_t tx[3] = {CMD_WRITE, addr, value};

    if (dev == NULL) {
        return MCP2515_ERR_INVALID_ARG;
    }

    return spi_transfer(dev, tx, NULL, sizeof(tx));
}

static mcp2515_err_t bit_modify(mcp2515_t *dev, uint8_t addr, uint8_t mask,
                                uint8_t data) {
    uint8_t tx[4] = {CMD_BIT_MODIFY, addr, mask, data};

    return spi_transfer(dev, tx, NULL, sizeof(tx));
}

static mcp2515_err_t wait_reg(mcp2515_t *dev, uint8_t addr, uint8_t mask,
                              uint8_t expected, uint32_t timeout_ms) {
    uint32_t start = HAL_GetTick();

    do {
        uint8_t value = 0;

        MCP2515_RETURN_ON_ERROR(mcp2515_read_reg(dev, addr, &value));
        if ((value & mask) == expected) {
            return MCP2515_OK;
        }
        HAL_Delay(1);
    } while ((HAL_GetTick() - start) < timeout_ms);

    return MCP2515_ERR_TIMEOUT;
}

static mcp2515_err_t reset(mcp2515_t *dev) {
    uint8_t cmd = CMD_RESET;

    MCP2515_RETURN_ON_ERROR(spi_transfer(dev, &cmd, NULL, 1));
    return wait_reg(dev, REG_CANSTAT, CANSTAT_OPMOD_MASK, OPMOD_CONFIG, 100);
}

mcp2515_err_t mcp2515_init(mcp2515_t *dev, SPI_HandleTypeDef *spi,
                           GPIO_TypeDef *cs_port, uint16_t cs_pin,
                           mcp2515_osc_t osc, uint32_t spi_timeout_ms) {
    if (dev == NULL || spi == NULL || cs_port == NULL ||
        osc >= MCP2515_OSC_COUNT) {
        return MCP2515_ERR_INVALID_ARG;
    }

    dev->spi = spi;
    dev->cs_port = cs_port;
    dev->cs_pin = cs_pin;
    dev->osc = osc;
    dev->spi_timeout_ms = (spi_timeout_ms == 0) ? 10 : spi_timeout_ms;

    HAL_GPIO_WritePin(dev->cs_port, dev->cs_pin, GPIO_PIN_SET);

    MCP2515_RETURN_ON_ERROR(reset(dev));
    MCP2515_RETURN_ON_ERROR(mcp2515_write_reg(dev, REG_RXB0CTRL, RXBCTRL_RXM_ANY));
    MCP2515_RETURN_ON_ERROR(mcp2515_write_reg(dev, REG_RXB1CTRL, RXBCTRL_RXM_ANY));

    return MCP2515_OK;
}

mcp2515_err_t mcp2515_probe(mcp2515_t *dev) {
    uint8_t canstat = 0;
    uint8_t canctrl = 0;
    bool mode_matches;
    bool clock_bits_ok;

    if (dev == NULL) {
        return MCP2515_ERR_INVALID_ARG;
    }

    MCP2515_RETURN_ON_ERROR(mcp2515_read_reg(dev, REG_CANSTAT, &canstat));
    MCP2515_RETURN_ON_ERROR(mcp2515_read_reg(dev, REG_CANCTRL, &canctrl));

    mode_matches = (canstat & CANSTAT_OPMOD_MASK) ==
                   (canctrl & CANSTAT_OPMOD_MASK);
    clock_bits_ok = (canctrl & 0x07) == 0x07;

    if (!mode_matches || !clock_bits_ok) {
        return MCP2515_ERR_NOT_FOUND;
    }
    return MCP2515_OK;
}

mcp2515_err_t mcp2515_get_status(mcp2515_t *dev, mcp2515_status_t *status) {
    if (dev == NULL || status == NULL) {
        return MCP2515_ERR_INVALID_ARG;
    }

    memset(status, 0, sizeof(*status));

    MCP2515_RETURN_ON_ERROR(mcp2515_read_reg(dev, REG_CANINTF, &status->canintf));
    MCP2515_RETURN_ON_ERROR(mcp2515_read_reg(dev, REG_EFLG, &status->eflg));

    status->bus_off = (status->eflg & EFLG_TXBO) != 0;
    status->rx_overflow = (status->eflg & (EFLG_RX0OVR | EFLG_RX1OVR)) != 0;
    status->error = (status->canintf & CANINTF_ERRIF) != 0;

    MCP2515_RETURN_ON_ERROR(bit_modify(dev, REG_CANINTF, CANINTF_ERRIF, 0));
    MCP2515_RETURN_ON_ERROR(bit_modify(dev, REG_EFLG,
                                       EFLG_RX0OVR | EFLG_RX1OVR, 0));

    return MCP2515_OK;
}

mcp2515_err_t mcp2515_selftest(mcp2515_t *dev) {
    static const uint8_t patterns[] = {0x55, 0xAA};
    uint8_t original = 0;
    mcp2515_err_t result = MCP2515_OK;
    mcp2515_err_t restore;

    if (dev == NULL) {
        return MCP2515_ERR_INVALID_ARG;
    }

    MCP2515_RETURN_ON_ERROR(mcp2515_read_reg(dev, REG_CNF1, &original));

    for (size_t i = 0; i < sizeof(patterns); i++) {
        uint8_t readback = 0;

        result = mcp2515_write_reg(dev, REG_CNF1, patterns[i]);
        if (result != MCP2515_OK) {
            break;
        }

        result = mcp2515_read_reg(dev, REG_CNF1, &readback);
        if (result != MCP2515_OK) {
            break;
        }

        if (readback != patterns[i]) {
            result = MCP2515_ERR_INVALID_RESPONSE;
            break;
        }
    }

    restore = mcp2515_write_reg(dev, REG_CNF1, original);
    if (restore != MCP2515_OK) {
        return restore;
    }

    return result;
}

mcp2515_err_t mcp2515_set_mode(mcp2515_t *dev, mcp2515_mode_t mode) {
    if (dev == NULL) {
        return MCP2515_ERR_INVALID_ARG;
    }

    MCP2515_RETURN_ON_ERROR(bit_modify(dev, REG_CANCTRL, CANSTAT_OPMOD_MASK,
                                       (uint8_t)mode));
    return wait_reg(dev, REG_CANSTAT, CANSTAT_OPMOD_MASK, (uint8_t)mode, 50);
}

mcp2515_err_t mcp2515_set_bitrate(mcp2515_t *dev, mcp2515_bitrate_t bitrate) {
    const bit_timing_t *t;
    uint8_t canctrl = 0;
    mcp2515_mode_t prev_mode;
    mcp2515_err_t err;
    mcp2515_err_t restore;

    if (dev == NULL || bitrate >= MCP2515_BITRATE_COUNT ||
        dev->osc >= MCP2515_OSC_COUNT) {
        return MCP2515_ERR_INVALID_ARG;
    }

    t = &s_timing[dev->osc][bitrate];
    if (t->cnf1 == BIT_TIMING_UNSUPPORTED) {
        return MCP2515_ERR_NOT_SUPPORTED;
    }

    MCP2515_RETURN_ON_ERROR(mcp2515_read_reg(dev, REG_CANCTRL, &canctrl));
    prev_mode = (mcp2515_mode_t)(canctrl & CANSTAT_OPMOD_MASK);

    MCP2515_RETURN_ON_ERROR(mcp2515_set_mode(dev, MCP2515_MODE_CONFIG));

    err = mcp2515_write_reg(dev, REG_CNF1, t->cnf1);
    if (err == MCP2515_OK) {
        err = mcp2515_write_reg(dev, REG_CNF2, t->cnf2);
    }
    if (err == MCP2515_OK) {
        err = mcp2515_write_reg(dev, REG_CNF3, t->cnf3);
    }

    restore = mcp2515_set_mode(dev, prev_mode);

    if (err != MCP2515_OK) {
        return err;
    }
    if (restore != MCP2515_OK) {
        return restore;
    }
    return MCP2515_OK;
}

mcp2515_err_t mcp2515_send(mcp2515_t *dev, const can_frame_t *frame,
                           uint32_t timeout_ms) {
    uint8_t ctrl = 0;
    uint8_t tx[TXB0_DATA + 8] = {
        [TXB0_CMD] = CMD_WRITE,
        [TXB0_ADDR] = REG_TXB0SIDH,
    };
    uint8_t rts = CMD_RTS_TXB0;
    mcp2515_err_t err;

    if (dev == NULL || frame == NULL || frame->dlc > 8 || frame->id > 0x7FF) {
        return MCP2515_ERR_INVALID_ARG;
    }

    MCP2515_RETURN_ON_ERROR(mcp2515_read_reg(dev, REG_TXB0CTRL, &ctrl));
    if (ctrl & TXBCTRL_TXREQ) {
        return MCP2515_ERR_INVALID_STATE;
    }

    tx[TXB0_SIDH] = (uint8_t)(frame->id >> 3);
    tx[TXB0_SIDL] = (uint8_t)((frame->id & 0x07) << 5);
    tx[TXB0_EID8] = 0;
    tx[TXB0_EID0] = 0;
    tx[TXB0_DLC] = frame->dlc;
    memcpy(&tx[TXB0_DATA], frame->data, frame->dlc);

    MCP2515_RETURN_ON_ERROR(spi_transfer(dev, tx, NULL, TXB0_DATA + frame->dlc));
    MCP2515_RETURN_ON_ERROR(spi_transfer(dev, &rts, NULL, 1));

    err = wait_reg(dev, REG_TXB0CTRL, TXBCTRL_TXREQ, 0, timeout_ms);
    if (err == MCP2515_ERR_TIMEOUT) {
        mcp2515_err_t abort_err = bit_modify(dev, REG_TXB0CTRL, TXBCTRL_TXREQ, 0);
        if (abort_err != MCP2515_OK) {
            return abort_err;
        }
        return err;
    }
    MCP2515_RETURN_ON_ERROR(err);

    MCP2515_RETURN_ON_ERROR(mcp2515_read_reg(dev, REG_TXB0CTRL, &ctrl));
    MCP2515_RETURN_ON_ERROR(bit_modify(dev, REG_CANINTF, CANINTF_TX0IF, 0));

    if (ctrl & (TXBCTRL_TXERR | TXBCTRL_MLOA | TXBCTRL_ABTF)) {
        return MCP2515_ERR_FAIL;
    }
    return MCP2515_OK;
}

mcp2515_err_t mcp2515_receive(mcp2515_t *dev, can_frame_t *frame,
                              uint32_t timeout_ms) {
    uint8_t tx[RXB0_DATA + RXB0_FRAME_LEN] = {
        [RXB0_CMD] = CMD_READ,
        [RXB0_ADDR] = REG_RXB0SIDH,
    };
    uint8_t rx[sizeof(tx)] = {0};
    const uint8_t *r;
    bool extended;
    bool remote;

    if (dev == NULL || frame == NULL) {
        return MCP2515_ERR_INVALID_ARG;
    }

    MCP2515_RETURN_ON_ERROR(wait_reg(dev, REG_CANINTF, CANINTF_RX0IF,
                                     CANINTF_RX0IF, timeout_ms));
    MCP2515_RETURN_ON_ERROR(spi_transfer(dev, tx, rx, sizeof(tx)));

    r = &rx[RXB0_DATA];
    extended = (r[1] & SIDL_IDE) != 0;
    remote = (r[4] & DLC_RTR) != 0;

    memset(frame->data, 0, sizeof(frame->data));
    frame->id = ((uint16_t)r[0] << 3) | (r[1] >> 5);
    frame->dlc = r[4] & DLC_MASK;
    if (frame->dlc > 8) {
        frame->dlc = 8;
    }
    memcpy(frame->data, &r[5], frame->dlc);

    MCP2515_RETURN_ON_ERROR(bit_modify(dev, REG_CANINTF, CANINTF_RX0IF, 0));

    if (extended || remote) {
        return MCP2515_ERR_NOT_SUPPORTED;
    }
    return MCP2515_OK;
}
