#pragma once

#include "stm32f4xx_hal.h"

extern SPI_HandleTypeDef hspi1;
extern UART_HandleTypeDef huart1;

void board_init(void);
void board_log(const char *fmt, ...);
uint32_t board_millis(void);
void board_led_toggle(void);
void Error_Handler(void);
