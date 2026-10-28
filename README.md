# Lilka–STM32 CAN Bridge

A two-node CAN bridge for connecting a [Lilka v2](https://github.com/and3rson/lilka) handheld console to an STM32-based node over a CAN bus using MCP2515 CAN controllers.

## Architecture

This project consists of two separate firmware nodes:

| Node | Platform | Purpose | Status |
|---|---|---|---|
| [`node-lilka`](./node-lilka) | ESP32-S3 (Lilka v2) | MCP2515 interface over SPI, CAN bridge endpoint | Functional (loopback test mode) |
| [`node-stm32`](./node-stm32) | STM32 (PlatformIO) | Second bridge node | Work in progress |

## Current Status

- **node-lilka**: Runs in CAN loopback mode at 500 kbit/s. Performs MCP2515 probe, self-test, and periodic loopback round-trips with error/status monitoring via `EFLG`.
- **node-stm32**: Project scaffold only, no firmware implemented yet.
- Real bus communication in `NORMAL` mode and the bridge logic between nodes are still to be implemented.

## Hardware

### node-lilka wiring (Lilka v2 ↔ MCP2515)

| Signal | GPIO |
| ------ | ---- |
| MOSI   | 21   |
| MISO   | 12   |
| SCLK   | 47   |
| CS     | 14   |

### TODO

- [ ] Confirm MCP2515 module and CAN transceiver
- [ ] Verify crystal oscillator frequency (firmware currently assumes `MCP2515_OSC_8MHZ`)
- [ ] Add CAN bus termination details
- [ ] Switch from `LOOPBACK` to `NORMAL` mode and test on real bus
- [ ] Implement `node-stm32` firmware and bridge protocol

## Building & Flashing

This project uses [PlatformIO](https://platformio.org/).

### node-lilka (Lilka v2)

```bash
cd node-lilka
pio run -e lilka_v2
pio run -e lilka_v2 -t upload
pio device monitor
```

### node-stm32

```bash
cd node-stm32
pio run
pio run -t upload
pio device monitor
```

## Project Structure

```text
.
├── node-lilka/   # ESP32-S3 firmware + MCP2515 driver component
└── node-stm32/   # STM32 firmware (scaffold)
```

## Development Notes

- The MCP2515 driver lives in `node-lilka/components/mcp2515/` with its own headers and implementation.
- Error flags (bus-off, RX overflow) are polled from `EFLG` as they aren't fully exposed via receive-path interrupt flags.
- Code style is enforced with `.clang-format` in `node-lilka/`.
