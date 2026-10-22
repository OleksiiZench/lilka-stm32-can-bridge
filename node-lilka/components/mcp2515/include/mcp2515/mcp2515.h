#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_err.h"

typedef enum {
    MCP2515_OSC_8MHZ,
    MCP2515_OSC_16MHZ,
    MCP2515_OSC_COUNT,
} mcp2515_osc_t;

typedef enum {
    MCP2515_BITRATE_125K,
    MCP2515_BITRATE_250K,
    MCP2515_BITRATE_500K,
    MCP2515_BITRATE_1M,
    MCP2515_BITRATE_COUNT,
} mcp2515_bitrate_t;

typedef enum {
    MCP2515_MODE_NORMAL      = 0x00,
    MCP2515_MODE_SLEEP       = 0x20,
    MCP2515_MODE_LOOPBACK    = 0x40,
    MCP2515_MODE_LISTEN_ONLY = 0x60,
    MCP2515_MODE_CONFIG      = 0x80,
} mcp2515_mode_t;

typedef struct {
    spi_host_device_t host;
    gpio_num_t mosi;
    gpio_num_t miso;
    gpio_num_t sclk;
    gpio_num_t cs;
    uint32_t clock_hz;
    mcp2515_osc_t osc;
} mcp2515_config_t;

typedef struct {
    spi_host_device_t host;
    spi_device_handle_t spi;
    mcp2515_osc_t osc;
} mcp2515_t;

typedef struct {
    uint16_t id;
    uint8_t dlc;
    uint8_t data[8];
} can_frame_t;

typedef struct {
    uint8_t canintf;   /* raw CANINTF register */
    uint8_t eflg;      /* raw EFLG register */
    bool bus_off;      /* EFLG.TXBO: controller lost the bus */
    bool rx_overflow;  /* EFLG.RX0OVR: a frame was lost */
    bool error;        /* CANINTF.ERRIF: bus error */
} mcp2515_status_t;

esp_err_t mcp2515_init(mcp2515_t *dev, const mcp2515_config_t *cfg);
void mcp2515_deinit(mcp2515_t *dev);
esp_err_t mcp2515_read_reg(mcp2515_t *dev, uint8_t addr, uint8_t *value);
esp_err_t mcp2515_probe(mcp2515_t *dev);
esp_err_t mcp2515_get_status(mcp2515_t *dev, mcp2515_status_t *status);
esp_err_t mcp2515_write_reg(mcp2515_t *dev, uint8_t addr, uint8_t value);
esp_err_t mcp2515_selftest(mcp2515_t *dev);

esp_err_t mcp2515_set_mode(mcp2515_t *dev, mcp2515_mode_t mode);
esp_err_t mcp2515_set_bitrate(mcp2515_t *dev, mcp2515_bitrate_t bitrate);
esp_err_t mcp2515_send(mcp2515_t *dev, const can_frame_t *frame, uint32_t timeout_ms);
esp_err_t mcp2515_receive(mcp2515_t *dev, can_frame_t *frame, uint32_t timeout_ms);
