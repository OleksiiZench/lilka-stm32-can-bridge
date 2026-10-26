# node-lilka

Firmware for the Lilka v2 node of the Lilka–STM32 CAN bridge. It drives an MCP2515 CAN
controller over SPI and currently runs a probe/selftest with a loopback roundtrip monitor.

## Build and flash

Run these commands from `node-lilka/`:

```sh
pio run
pio run -t upload
pio device monitor
```

## Hardware

CAN pins from `include/board_pins.h`:

| Signal | GPIO |
| ------ | ---- |
| MOSI   | 21   |
| MISO   | 12   |
| SCLK   | 47   |
| CS     | 14   |

- MCP2515 module model: TODO
- Crystal frequency: TODO (firmware currently assumes `MCP2515_OSC_8MHZ`; verify)
- Transceiver: TODO
- Termination: TODO
