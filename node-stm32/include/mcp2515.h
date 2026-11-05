#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "stm32f4xx_hal.h"

typedef enum {
    MCP2515_OK = 0,
    MCP2515_ERR_INVALID_ARG,
    MCP2515_ERR_INVALID_STATE,
    MCP2515_ERR_TIMEOUT,
    MCP2515_ERR_NOT_FOUND,
    MCP2515_ERR_NOT_SUPPORTED,
    MCP2515_ERR_IO,
    MCP2515_ERR_INVALID_RESPONSE,
    MCP2515_ERR_FAIL,
} mcp2515_err_t;

typedef enum {
    MCP2515_OSC_8MHZ = 0,
    MCP2515_OSC_16MHZ,
    MCP2515_OSC_COUNT,
} mcp2515_osc_t;

typedef enum {
    MCP2515_BITRATE_125K = 0,
    MCP2515_BITRATE_250K,
    MCP2515_BITRATE_500K,
    MCP2515_BITRATE_1M,
    MCP2515_BITRATE_COUNT,
} mcp2515_bitrate_t;

typedef enum {
    MCP2515_MODE_NORMAL = 0x00,
    MCP2515_MODE_SLEEP = 0x20,
    MCP2515_MODE_LOOPBACK = 0x40,
    MCP2515_MODE_LISTEN_ONLY = 0x60,
    MCP2515_MODE_CONFIG = 0x80,
} mcp2515_mode_t;

typedef struct {
    SPI_HandleTypeDef *spi;
    GPIO_TypeDef *cs_port;
    uint16_t cs_pin;
    mcp2515_osc_t osc;
    uint32_t spi_timeout_ms;
} mcp2515_t;

typedef struct {
    uint16_t id;
    uint8_t dlc;
    uint8_t data[8];
} can_frame_t;

typedef struct {
    uint8_t canintf;
    uint8_t eflg;
    bool bus_off;
    bool rx_overflow;
    bool error;
} mcp2515_status_t;

mcp2515_err_t mcp2515_init(mcp2515_t *dev, SPI_HandleTypeDef *spi,
                           GPIO_TypeDef *cs_port, uint16_t cs_pin,
                           mcp2515_osc_t osc, uint32_t spi_timeout_ms);

mcp2515_err_t mcp2515_read_reg(mcp2515_t *dev, uint8_t addr, uint8_t *value);
mcp2515_err_t mcp2515_write_reg(mcp2515_t *dev, uint8_t addr, uint8_t value);
mcp2515_err_t mcp2515_probe(mcp2515_t *dev);
mcp2515_err_t mcp2515_selftest(mcp2515_t *dev);
mcp2515_err_t mcp2515_set_mode(mcp2515_t *dev, mcp2515_mode_t mode);
mcp2515_err_t mcp2515_set_bitrate(mcp2515_t *dev, mcp2515_bitrate_t bitrate);
mcp2515_err_t mcp2515_send(mcp2515_t *dev, const can_frame_t *frame, uint32_t timeout_ms);
mcp2515_err_t mcp2515_receive(mcp2515_t *dev, can_frame_t *frame, uint32_t timeout_ms);
mcp2515_err_t mcp2515_get_status(mcp2515_t *dev, mcp2515_status_t *status);
