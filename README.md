# TRAK (Telemetry Remote Analytics Kernel) 🏍️

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![Firmware](https://img.shields.io/badge/Firmware-ESP32-blue.svg)](trak-firmware/)
[![Backend](https://img.shields.io/badge/Backend-Bun-orange.svg)](trak-backend/)

**TRAK** is a high-performance telemetry ingestion and analytics platform designed specifically for motorcycles. It captures real-time engine data (OBD-II) and motion data (IMU) using an ESP32, transmitting it via MQTT to a scalable backend for processing and storage.

---

## 🚀 Overview

The system is designed to handle high-frequency data (up to 10Hz) with reliability over flaky WiFi/Cellular connections. It uses a **non-destructive batching** strategy to ensure data integrity while minimizing network overhead.

### **Core Stack**
- **Hardware**: ESP32 (Firmware written in Arduino/C++ with FreeRTOS).
- **Protocol**: MQTT (via Mosquitto).
- **Backend**: Bun + TypeScript.
- **Database**: TimescaleDB (PostgreSQL) for time-series metrics.
- **Storage**: Local Filesystem (JSON) for raw audit logs.

---

## ✨ Features

- **High-Frequency Sampling**: Samples IMU and OBD-II data in parallel using FreeRTOS tasks.
- **Non-Destructive Batching**: Buffers readings in memory and publishes them in optimized JSON batches.
- **Auto-Recovery**: Handles MQTT reconnections and WiFi drops gracefully.
- **Hybrid Storage**:
    - **TimescaleDB**: Stores parsed and computed metrics (RPM, Speed, Lean Angle, G-Force) for fast analytics.
    - **Local JSON**: Stores raw MQTT payloads for auditability and debugging without database bloat.
- **Lean Angle Calculation**: Real-time pitch/roll computation from raw accelerometer/gyroscope data.

---

## 📂 Project Structure

```text
.
├── trak-backend/         # Bun-based ingestion server
│   ├── src/handlers/     # Ingestion logic (Telemetry, Crashes, Status)
│   ├── src/db/           # Drizzle ORM + TimescaleDB client
│   └── storage/          # (Ignored) Local raw telemetry storage
├── trak-firmware/        # ESP32 Firmware
│   └── main/             # Core Arduino sketch + FreeRTOS tasks
├── docs/                 # Detailed technical specifications
└── setup.sh              # Project initialization script
```

---

## 🛠️ Getting Started

### **1. Backend Setup**
Navigate to the backend directory and spin up the infrastructure:
```bash
cd trak-backend
cp .env.example .env
docker-compose up -d
bun install
bun run dev
```

### **2. Firmware Setup**
1. Open `trak-firmware/main/main.ino` in VS Code (with PlatformIO or Arduino extension).
2. Create `secrets.h` from the example:
   ```cpp
   cp trak-firmware/main/secrets.h.example trak-firmware/main/secrets.h
   ```
3. Update your WiFi and MQTT credentials.
4. Flash to your ESP32.

---

## 📊 Telemetry Data Model

The platform processes three main types of messages:
- **Telemetry**: High-frequency batches of engine and motion data.
- **Status**: Periodic heartbeat with device vitals (Heap, RSSI, Uptime).
- **Crash**: Guru Meditation or exception reports for remote debugging.

---

## 📜 License

Distributed under the MIT License. See `LICENSE` for more information.

---

*Built with ❤️ for the ride.*
