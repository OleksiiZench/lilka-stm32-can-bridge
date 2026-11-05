# Lilka–STM32 CAN Bridge

A two-node CAN bridge for connecting a [Lilka v2](https://github.com/and3rson/lilka) handheld console to an STM32-based node over a CAN bus using MCP2515 CAN controllers.

## Architecture

This project consists of two separate firmware nodes:

| Node | Platform | Purpose | Status |
|---|---|---|---|
| [`node-lilka`](./node-lilka) | ESP32-S3 (Lilka v2) | MCP2515 interface over SPI, CAN bridge endpoint | Functional (loopback test mode) |
| [`node-stm32`](./node-stm32) | STM32 Blackpill F411CE | Second bridge node, same MCP2515 + TJA1050 module | Work in progress |

## Current Status

- **Hardware (both nodes)**: MCP2515 CAN controller + TJA1050 5 V transceiver, 8 MHz crystal, onboard 120 Ω termination.
- **node-lilka**: Runs in CAN loopback mode at 500 kbit/s. Performs MCP2515 probe, self-test, and periodic loopback round-trips with error/status monitoring via `EFLG`.
- **node-stm32**: Project scaffold only, no firmware implemented yet.
- Real bus communication in `NORMAL` mode and the bridge logic between nodes are still to be implemented.

## Hardware

Both nodes use the same CAN module: **MCP2515** controller + **TJA1050** 5 V transceiver, **8 MHz** crystal and an onboard **120 Ω** termination resistor.

### node-lilka wiring (Lilka v2 ↔ MCP2515)

| Signal | GPIO |
| ------ | ---- |
| MOSI   | 21   |
| MISO   | 12   |
| SCLK   | 47   |
| CS     | 14   |

Module pinout: `INT`, `SCK`, `SI`, `SO`, `CS`, `GND`, `VCC`.

- The module is powered from a **separate 5 V supply** (not from Lilka).
- `SO` (MISO) is a 5 V output; it is connected to Lilka through a **resistor divider** to 3.3 V.
- `SI`, `SCK`, `CS` are driven by Lilka at 3.3 V.
- `INT` is not connected; the driver polls `CANINTF`/`EFLG`.
- The module supply and Lilka must share a common ground.

### node-stm32 wiring

Second node: **STM32 Blackpill F411CE** with the same **MCP2515 + TJA1050** module (5 V, 8 MHz, 120 Ω). Firmware is not implemented yet.

### CAN bus

| Node | CANH | CANL | Termination |
| ---- | ---- | ---- | ----------- |
| node-lilka | CANH | CANL | 120 Ω onboard |
| node-stm32 | CANH | CANL | 120 Ω onboard |

- Connect `CANH ↔ CANH`, `CANL ↔ CANL`, and a common ground.
- With both nodes terminated, the measured resistance across `CANH`/`CANL` is ~60 Ω.
- Each module already has its own 120 Ω resistor; do not add a third termination on the bus.

### TODO

- [x] Confirm MCP2515 module and CAN transceiver
- [x] Verify crystal oscillator frequency (firmware currently assumes `MCP2515_OSC_8MHZ`)
- [x] Add CAN bus termination details
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
