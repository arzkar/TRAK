# wiring.spec.md
# TRAK — Wiring Specification

---

## Overview

All V1 connections are made on an 830-point solderless breadboard using jumper wires. No permanent soldering until Phase 3 (perfboard migration). All connections are reversible and non-destructive to the bike.

---

## Power Rail

### Bike → Buck Converter
```
CB350 fuse box (switched accessory fuse, +12V)
    │
    ├── Fuse tap (add-a-fuse adapter)
    │       │
    │       └── +12V wire (RED) → inline 2A blade fuse → Buck converter IN+
    │
    └── Ground wire (BLACK) → chassis frame bolt (GND) → Buck converter IN−
```

### Buck Converter → ESP32
```
Buck converter OUT+ (5.0V) → ESP32 VIN        (RED wire)
Buck converter OUT−        → ESP32 GND        (BLACK wire)
```

**Critical:** Set buck converter output to exactly 5.0V using the trim pot and onboard display BEFORE connecting ESP32. Verify with display. Do not proceed until stable at 5.0V.

### Breadboard Power Rails
```
Breadboard + rail → ESP32 3.3V pin    (powers MPU6050 + SD module)
Breadboard − rail → ESP32 GND pin     (common ground for all modules)
```

---

## MPU6050 → ESP32 (I²C)

| MPU6050 Pin | ESP32 Pin | Wire Color | Notes |
|---|---|---|---|
| VCC | 3.3V | Red | From breadboard + rail |
| GND | GND | Black | From breadboard − rail |
| SCL | GPIO 22 | Yellow | Default ESP32 I²C clock |
| SDA | GPIO 21 | Blue | Default ESP32 I²C data |
| AD0 | GND | Black | Sets I²C address to 0x68 |
| INT | GPIO 4 | Purple | Optional — interrupt-driven sampling |
| XDA | Not connected | — | Auxiliary I²C, unused |
| XCL | Not connected | — | Auxiliary I²C, unused |

**Notes:**
- Wire.begin() in Arduino uses GPIO 21/22 by default — no configuration needed
- AD0 tied to GND gives address 0x68. Tie to 3.3V for 0x69 if running two MPU6050s
- INT pin enables interrupt-driven reads instead of polling — connect for production firmware

---

## MicroSD Module → ESP32 (SPI — VSPI bus)

| SD Module Pin | ESP32 Pin | Wire Color | Notes |
|---|---|---|---|
| VCC | 3.3V | Red | Module has onboard level shifter |
| GND | GND | Black | |
| MISO | GPIO 19 | Blue | SPI data out from SD card |
| MOSI | GPIO 23 | Green | SPI data in to SD card |
| SCK | GPIO 18 | Yellow | SPI clock |
| CS | GPIO 5 | Purple | Chip select — SD card |

**Notes:**
- GPIO 18/19/23 are the hardware VSPI bus on ESP32
- MCP2515 (V2) shares same MISO/MOSI/SCK — uses GPIO 15 for CS
- SD library: SD.begin(5) — pass CS pin number

---

## MCP2515 CAN Module → ESP32 (SPI — V2, shares VSPI bus)

| MCP2515 Pin | ESP32 Pin | Wire Color | Notes |
|---|---|---|---|
| VCC | 5V | Red | MCP2515 requires 5V — use VIN pin (pre-regulator) |
| GND | GND | Black | |
| MISO | GPIO 19 | Blue | Shared with SD card |
| MOSI | GPIO 23 | Green | Shared with SD card |
| SCK | GPIO 18 | Yellow | Shared with SD card |
| CS | GPIO 15 | Orange | Separate CS from SD card |
| INT | GPIO 2 | Purple | CAN interrupt pin |

**Notes:**
- MCP2515 runs at 5V — SPI signals are 5V. Use level shifter on MISO/MOSI/SCK/CS lines
- Only one SPI device is active at a time — managed by CS pin states
- CAN bus requires 120Ω termination resistor between CAN-H and CAN-L at each end

---

## ELM327 → ESP32

No physical wiring. ELM327 connects to CB350 OBD port via iovi cable. ESP32 connects to ELM327 over Bluetooth Classic SPP using BluetoothSerial library.

---

## Bike Connection Points

| Connection | Location on CB350 | Method |
|---|---|---|
| +12V power | Fuse box under seat, accessory fuse slot | Fuse tap (add-a-fuse) |
| Ground | Frame chassis bolt near fuse box | Ring terminal on bolt |
| OBD data | Red 6-pin diagnostic port under seat | iovi adapter cable |

All connections are non-destructive. Full removal in under 60 seconds.

---

## Full GPIO Allocation

| GPIO | Function | Protocol | Module |
|---|---|---|---|
| 4 | IMU interrupt | Digital in | MPU6050 INT |
| 5 | SD card chip select | SPI CS | MicroSD |
| 15 | CAN chip select (V2) | SPI CS | MCP2515 |
| 18 | SPI clock | SPI SCK | SD + MCP2515 |
| 19 | SPI MISO | SPI | SD + MCP2515 |
| 21 | I²C data | I²C SDA | MPU6050 |
| 22 | I²C clock | I²C SCL | MPU6050 |
| 23 | SPI MOSI | SPI | SD + MCP2515 |
| 2 | CAN interrupt (V2) | Digital in | MCP2515 INT |
| VIN | 5V power input | Power | Buck converter |
| 3.3V | 3.3V output | Power | MPU6050, SD |
| GND | Common ground | Power | All modules |

**Available GPIO (unused in V1):** 0, 12, 13, 14, 16, 17, 25, 26, 27, 32, 33, 34, 35, 36, 39

---

## I²C Bus Summary

| Device | Address | SDA | SCL |
|---|---|---|---|
| MPU6050 | 0x68 | GPIO 21 | GPIO 22 |

---

## SPI Bus Summary (VSPI)

| Device | MISO | MOSI | SCK | CS |
|---|---|---|---|---|
| MicroSD | GPIO 19 | GPIO 23 | GPIO 18 | GPIO 5 |
| MCP2515 (V2) | GPIO 19 | GPIO 23 | GPIO 18 | GPIO 15 |

---

## Wiring Rules

1. Never connect two CS pins HIGH simultaneously on the same SPI bus
2. All GND pins share a common ground rail on the breadboard
3. 3.3V rail powers MPU6050 and SD module only — do not exceed 500mA on 3.3V rail
4. MCP2515 VCC connects to ESP32 VIN (5V), not the 3.3V rail
5. Verify all connections with a multimeter continuity check before powering on
6. Power up sequence: buck converter → breadboard rails → connect modules → ESP32 last
