#pragma once

#include "stm32f4xx_hal.h"

#define CAN_SPI_INSTANCE        SPI1

#define CAN_SCK_GPIO_PORT       GPIOA
#define CAN_SCK_PIN             GPIO_PIN_5
#define CAN_MISO_GPIO_PORT      GPIOA
#define CAN_MISO_PIN            GPIO_PIN_6
#define CAN_MOSI_GPIO_PORT      GPIOA
#define CAN_MOSI_PIN            GPIO_PIN_7
#define CAN_CS_GPIO_PORT        GPIOA
#define CAN_CS_PIN              GPIO_PIN_4

#define LOG_UART_INSTANCE       USART1
#define LOG_UART_GPIO_PORT      GPIOA
#define LOG_UART_TX_PIN         GPIO_PIN_9
#define LOG_UART_RX_PIN         GPIO_PIN_10
#define LOG_UART_BAUD           115200

#define LED_GPIO_PORT           GPIOC
#define LED_PIN                 GPIO_PIN_13
