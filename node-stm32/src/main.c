#include "app_config.h"
#include "board.h"
#include "board_pins.h"
#include "mcp2515.h"

static mcp2515_t s_can;
static bool s_can_ready = false;
static uint32_t s_last_rx_ms = 0;
static uint32_t s_rx_count = 0;
static uint8_t s_last_rx_counter = 0;
static uint8_t s_tx_counter = 0;

static mcp2515_err_t can_init(void) {
    mcp2515_err_t err;

    err = mcp2515_init(&s_can, &hspi1, CAN_CS_GPIO_PORT, CAN_CS_PIN,
                       MCP2515_OSC_8MHZ, 10);
    if (err != MCP2515_OK) {
        return err;
    }

    err = mcp2515_probe(&s_can);
    if (err != MCP2515_OK) {
        return err;
    }

    err = mcp2515_selftest(&s_can);
    if (err != MCP2515_OK) {
        return err;
    }

    err = mcp2515_set_bitrate(&s_can, MCP2515_BITRATE_500K);
    if (err != MCP2515_OK) {
        return err;
    }

    return mcp2515_set_mode(&s_can, MCP2515_MODE_NORMAL);
}

static void can_poll_rx(void) {
    can_frame_t frame;

    while (1) {
        mcp2515_err_t err = mcp2515_receive(&s_can, &frame, 0);

        if (err == MCP2515_ERR_TIMEOUT) {
            break;
        }
        if (err == MCP2515_ERR_NOT_SUPPORTED) {
            continue;
        }
        if (err != MCP2515_OK) {
            break;
        }

        if (frame.id == APP_PARTNER_ID) {
            s_last_rx_ms = board_millis();
            s_rx_count++;
            if (frame.dlc >= 4) {
                s_last_rx_counter = frame.data[3];
            }
        }
    }
}

static mcp2515_err_t can_send_heartbeat(void) {
    can_frame_t tx = {0};

    tx.id = APP_NODE_ID;
    tx.dlc = 4;
    tx.data[0] = 'S';
    tx.data[1] = 'T';
    tx.data[2] = 'M';
    tx.data[3] = s_tx_counter++;

    return mcp2515_send(&s_can, &tx, 100);
}

static void can_log_status(void) {
    mcp2515_status_t status = {0};
    mcp2515_err_t err = mcp2515_get_status(&s_can, &status);
    uint32_t now = board_millis();
    bool link_up = (s_rx_count > 0) &&
                   ((now - s_last_rx_ms) < APP_LINK_TIMEOUT_MS);

    if (link_up) {
        board_log("[%s] CAN: link up | rx=%lu last=%u age=%lu ms",
                  APP_NODE_NAME,
                  (unsigned long)s_rx_count,
                  (unsigned)s_last_rx_counter,
                  (unsigned long)(now - s_last_rx_ms));
    } else {
        board_log("[%s] CAN: no link | rx=%lu",
                  APP_NODE_NAME,
                  (unsigned long)s_rx_count);
    }

    if (err == MCP2515_OK) {
        board_log(" | EFLG=0x%02X CANINTF=0x%02X\r\n",
                  (unsigned)status.eflg,
                  (unsigned)status.canintf);
    } else {
        board_log(" | status err=%d\r\n", (int)err);
    }

    if (err == MCP2515_OK && status.bus_off) {
        board_log("[%s] bus off, reinit\r\n", APP_NODE_NAME);
        s_can_ready = false;
    }
}

int main(void) {
    board_init();

    board_log("\r\n[%s] start\r\n", APP_NODE_NAME);
    board_log("[%s] sysclk: %lu Hz\r\n",
              APP_NODE_NAME, (unsigned long)SystemCoreClock);

    uint32_t last_led = 0;
    uint32_t last_tick = 0;

    while (1) {
        if (s_can_ready) {
            can_poll_rx();
        }

        uint32_t now = board_millis();

        if (now - last_led >= 500u) {
            last_led = now;
            board_led_toggle();
        }

        if (now - last_tick < APP_TX_PERIOD_MS) {
            continue;
        }
        last_tick = now;

        if (!s_can_ready) {
            mcp2515_err_t err = can_init();
            s_can_ready = (err == MCP2515_OK);

            board_log("[%s] MCP2515: %s (err=%d)\r\n",
                      APP_NODE_NAME,
                      s_can_ready ? "OK" : "FAIL",
                      (int)err);
            continue;
        }

        mcp2515_err_t err = can_send_heartbeat();

        if (err == MCP2515_ERR_INVALID_STATE || err == MCP2515_ERR_IO) {
            board_log("[%s] send failed: %d, reinit\r\n",
                      APP_NODE_NAME, (int)err);
            s_can_ready = false;
            continue;
        }
        if (err != MCP2515_OK) {
            board_log("[%s] send failed: %d\r\n",
                      APP_NODE_NAME, (int)err);
        }

        can_log_status();
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
