#pragma once

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_err.h"

typedef struct {
    spi_host_device_t host;
    gpio_num_t mosi;
    gpio_num_t miso;
    gpio_num_t sclk;
    gpio_num_t cs;
    int clock_hz;
} mcp2515_config_t;

typedef struct {
    spi_device_handle_t spi;
} mcp2515_t;

esp_err_t mcp2515_init(mcp2515_t *dev, const mcp2515_config_t *cfg);

esp_err_t mcp2515_read_reg(mcp2515_t *dev, uint8_t addr, uint8_t *value);

esp_err_t mcp2515_probe(mcp2515_t *dev);
