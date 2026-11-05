# node-lilka

Firmware for the Lilka v2 node of the Lilka–STM32 CAN bridge. It drives an MCP2515 CAN controller over SPI and currently runs in **LOOPBACK** mode at **500 kbit/s**: probe, register self-test, and a periodic loopback round-trip monitor.

## Build and flash

Run these commands from `node-lilka/`:

```sh
pio run -e lilka_v2
pio run -e lilka_v2 -t upload
pio device monitor
```

## Hardware

Both nodes use the same module: **MCP2515** controller + **TJA1050** 5 V transceiver,
**8 MHz** crystal, onboard **120 Ω** termination.

CAN pins from `include/board_pins.h`:

| Signal | GPIO |
| ------ | ---- |
| MOSI   | 21   |
| MISO   | 12   |
| SCLK   | 47   |
| CS     | 14   |

Module pinout: `INT`, `SCK`, `SI`, `SO`, `CS`, `GND`, `VCC`.

- MCP2515 module: MCP2515 + TJA1050
- Crystal frequency: 8 MHz (confirmed, `MCP2515_OSC_8MHZ`)
- Transceiver: TJA1050 (5 V)
- Termination: 120 Ω onboard
- Module power: separate 5 V supply; common ground with Lilka
- Logic levels: `SO` → resistor divider → 3.3 V; `SI`/`SCK`/`CS` driven at 3.3 V
- `INT`: not connected (driver polls `CANINTF`/`EFLG`)

### CAN bus

The second node is `node-stm32` (STM32 Blackpill F411CE) with the same MCP2515 + TJA1050
module. Connect `CANH ↔ CANH`, `CANL ↔ CANL`, and a common ground. Both modules have
120 Ω termination, so the bus measures ~60 Ω across `CANH`/`CANL`.

### TODO

- [ ] Switch from `LOOPBACK` to `NORMAL` mode and test on a real bus
- [ ] Implement bridge logic between the CAN bus and the STM32 node

