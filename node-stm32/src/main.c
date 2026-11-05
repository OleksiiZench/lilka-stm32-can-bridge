#include "board.h"
#include "app_config.h"
#include "board_pins.h"
#include "mcp2515.h"

static mcp2515_t can;

static void can_bringup_test(void) {
    mcp2515_err_t err;
    can_frame_t tx = {
        .id = 0x123,
        .dlc = 8,
        .data = {0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x00, 0x00, 0x42},
    };
    can_frame_t rx = {0};

    err = mcp2515_init(&can, &hspi1, CAN_CS_GPIO_PORT, CAN_CS_PIN,
                       MCP2515_OSC_8MHZ, 10);
    board_log("[STM32] mcp init: %d\r\n", (int)err);
    if (err != MCP2515_OK) {
        return;
    }

    err = mcp2515_probe(&can);
    board_log("[STM32] probe: %d\r\n", (int)err);
    if (err != MCP2515_OK) {
        return;
    }

    err = mcp2515_selftest(&can);
    board_log("[STM32] selftest: %d\r\n", (int)err);

    err = mcp2515_set_bitrate(&can, MCP2515_BITRATE_500K);
    board_log("[STM32] bitrate: %d\r\n", (int)err);
    if (err != MCP2515_OK) {
        return;
    }

    err = mcp2515_set_mode(&can, MCP2515_MODE_LOOPBACK);
    board_log("[STM32] loopback: %d\r\n", (int)err);
    if (err != MCP2515_OK) {
        return;
    }

    err = mcp2515_send(&can, &tx, 100);
    board_log("[STM32] send: %d\r\n", (int)err);
    if (err != MCP2515_OK) {
        return;
    }

    err = mcp2515_receive(&can, &rx, 100);
    board_log("[STM32] recv: %d\r\n", (int)err);
    if (err == MCP2515_OK) {
        board_log("[STM32] rx id=0x%03X dlc=%u d7=0x%02X\r\n",
                  (unsigned)rx.id, (unsigned)rx.dlc, (unsigned)rx.data[7]);
    }
}

int main(void) {
    board_init();

    board_log("\r\n[%s] start\r\n", APP_NODE_NAME);
    board_log("[%s] sysclk: %lu Hz\r\n", APP_NODE_NAME, (unsigned long)SystemCoreClock);

    can_bringup_test();
    
    uint32_t last_led = 0;
    uint32_t last_log = 0;

    while (1) {
        uint32_t now = board_millis();

        if (now - last_led >= 500u) {
            last_led = now;
            board_led_toggle();
        }

        if (now - last_log >= 1000u) {
            last_log = now;
            board_log("[%s] alive, uptime=%lu s\r\n",
                      APP_NODE_NAME, (unsigned long)(now / 1000u));
        }
    }
}

void Error_Handler(void) {
    __disable_irq();
    while (1) {
    }
}

void SysTick_Handler(void) {
    HAL_IncTick();
}

void NMI_Handler(void) {
}

void HardFault_Handler(void) {
    while (1) {
    }
}

void MemManage_Handler(void) {
    while (1) {
    }
}

void BusFault_Handler(void) {
    while (1) {
    }
}

void UsageFault_Handler(void) {
    while (1) {
    }
}

void SVC_Handler(void) {
}

void DebugMon_Handler(void) {
}

void PendSV_Handler(void) {
}