#pragma once

#include <stdbool.h>

#include "stm32f4xx_hal.h"

extern SPI_HandleTypeDef hspi1;
extern UART_HandleTypeDef huart1;

void board_init(void);
void board_log(const char *fmt, ...);
uint32_t board_millis(void);
void board_led_toggle(void);
void board_led_set(bool on);
void Error_Handler(void);
