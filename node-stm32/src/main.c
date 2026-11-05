#include "board.h"
#include "app_config.h"

int main(void) {
    board_init();

    board_log("\r\n[%s] start\r\n", APP_NODE_NAME);
    board_log("[%s] sysclk: %lu Hz\r\n", APP_NODE_NAME, (unsigned long)SystemCoreClock);

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