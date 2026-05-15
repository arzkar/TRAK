# hardware.spec.md
# TRAK — Hardware Specification

---

## Microcontroller

| Property | Value |
|---|---|
| Board | ESP32-WROOM-32D 30-pin |
| USB interface | USB-C (CH340C driver) |
| CPU | Xtensa dual-core LX6, 240MHz |
| Flash | 4MB SPI flash |
| SRAM | 520KB |
| WiFi | 802.11 b/g/n 2.4GHz |
| Bluetooth | Classic BT 4.2 + BLE |
| Operating voltage | 3.3V logic, 5V VIN |
| Deep sleep current | <5µA |
| Source | Probots |
| Price | ₹500 |

---

## IMU — MPU6050 GY-521

| Property | Value |
|---|---|
| Sensor | InvenSense MPU-6050 |
| Axes | 3-axis accelerometer + 3-axis gyroscope |
| Interface | I²C (up to 400kHz) |
| I²C address | 0x68 (AD0=GND), 0x69 (AD0=VCC) |
| Supply voltage | 2.3V–3.4V (module has onboard LDO, accepts 3.3V or 5V) |
| Accelerometer range | ±2g / ±4g / ±8g / ±16g (programmable) |
| Gyroscope range | ±250 / ±500 / ±1000 / ±2000 °/s (programmable) |
| ADC resolution | 16-bit |
| Sampling rate | Up to 8kHz (gyro), 1kHz (accel) |
| Operating temp | -40°C to +85°C |
| Source | Robocraze / Probots |
| Price | ₹150 |

**Configuration for TRAK:**
- Accelerometer range: ±8g (covers crash detection up to ~8g)
- Gyroscope range: ±500°/s (covers aggressive lean transitions)
- DLPF bandwidth: 44Hz (filters road vibration above 44Hz)
- Sample rate: 100Hz effective

---

## Buck Converter — LM2596 with Display

| Property | Value |
|---|---|
| IC | LM2596 |
| Input voltage | 4.75V–35V |
| Output voltage | 1.25V–26V adjustable |
| Output current | Rated 2A, max 3A |
| Switching frequency | 150kHz |
| Efficiency | Up to 92% |
| Set output | 5.0V exactly |
| Source | Robocraze |
| Price | ₹120 |

**Power budget:**
| Component | Current |
|---|---|
| ESP32 (WiFi active) | ~240mA peak, ~80mA avg |
| MPU6050 | ~3.9mA |
| SD card module | ~100mA peak write, ~1mA idle |
| Total peak | ~350mA |
| Total average | ~85mA |

---

## MicroSD Module

| Property | Value |
|---|---|
| Interface | SPI |
| Supply voltage | 3.3V / 5V (onboard level shifter) |
| Card support | MicroSD / MicroSDHC up to 32GB FAT32 |
| Source | Robocraze / Probots |
| Price | ₹60 |

Storage estimate: ~2KB per 30s batch → ~5.7MB per hour → 8GB lasts ~1,400 hours

---

## ELM327 Bluetooth OBD2

| Property | Value |
|---|---|
| Version | v2.1 |
| Connectivity | Bluetooth Classic SPP |
| Protocols | ISO 9141-2, ISO 14230 KWP2000, ISO 15765 CAN, SAE J1850 |
| Power | OBD port pin 16 (+12V switched) |
| Current draw | ~50mA |
| Default BT name | "OBDII" or "V-LINK" |
| Default PIN | 1234 |
| Source | Robocraze |
| Price | ₹185 |

---

## iovi Honda OBD Adapter Cable

| Property | Value |
|---|---|
| Connector A | Honda 6-pin diagnostic (BS4/BS6) |
| Connector B | OBD2 16-pin female |
| Protocols | K-Line + CAN Bus |
| Compatible | Honda CB350, CB350RS, Honda BS6 range |
| Source | Amazon India |
| Price | ₹300 |

---

## MCP2515 CAN Bus Module (V2)

| Property | Value |
|---|---|
| CAN controller | Microchip MCP2515 |
| Transceiver | TJA1050 |
| Interface | SPI |
| CAN standard | v2.0B |
| Max speed | 1Mbps |
| Supply | 5V |
| Source | Robocraze / Probots |
| Price | ₹100 |

---

## V1 Bill of Materials — Total ₹1,775

| Component | Price |
|---|---|
| ESP32-WROOM-32D USB-C | ₹500 |
| MPU6050 GY-521 | ₹150 |
| LM2596 buck converter with display | ₹120 |
| MicroSD module | ₹60 |
| ELM327 Bluetooth OBD2 | ₹185 |
| iovi Honda 6-pin to OBD2 cable | ₹300 |
| 830-point breadboard | ₹100 |
| Jumper wire set (M-M, M-F, F-F 40pc each) | ₹150 |
| MicroSD card 8GB Class 10 | ₹150 |
| Fuse tap + inline fuse holder + 2A fuse | ₹60 |

---

## Electrical Constraints

- ESP32 GPIO is 3.3V logic — never apply 5V directly to any GPIO pin
- MPU6050: power from 3.3V rail, not 5V
- MCP2515 (V2): operates at 5V — level shifter required on SPI lines
- Always verify buck converter output at 5.0V before connecting ESP32
- 2A inline fuse required between fuse tap and buck converter IN+
- Ground to bike chassis frame bolt, not directly to battery negative
- Operating temperature range: -10°C to +70°C
- V1 has no IP rating — use sealed project box under seat
