# 📦 PackageGuard Pro

> **Real-time IoT system for monitoring and securing valuable shipments.**  
> Built with ESP32, Flask, MQTT, and Android — featuring hybrid BLE/WiFi communication and offline-first design.

![Platform](https://img.shields.io/badge/platform-ESP32-blue?style=flat-square)
![Python](https://img.shields.io/badge/Python-3.x-yellow?style=flat-square&logo=python)
![Android](https://img.shields.io/badge/Android-Kotlin%2FJava-green?style=flat-square&logo=android)
![Protocol](https://img.shields.io/badge/protocol-MQTT%20%7C%20BLE-orange?style=flat-square)
![Course](https://img.shields.io/badge/AGH%20UST-IoT%20Sem.%205-red?style=flat-square)

---

## 📌 Overview

**PackageGuard Pro** is a full-stack IoT solution designed to protect valuable packages in transit. The device continuously monitors environmental conditions and physical integrity, sending real-time alerts when anomalies are detected — even without an internet connection.

The system consists of three tightly integrated components:

| Component | Technology | Role |
|-----------|-----------|------|
| **Embedded Device** | ESP32 (C/Arduino) | Sensor hub, alarm engine, BLE + MQTT client |
| **Server Application** | Python / Flask / Paho-MQTT | Data ingestion, user management, web dashboard |
| **Mobile Application** | Android | BLE pairing, offline control, telemetry viewer |

---

## ✨ Features

### 🔩 Device (ESP32)
- **Environmental monitoring** — temperature, humidity, atmospheric pressure, light intensity (BME280)
- **Shock & tamper detection** — accelerometer-based fall and impact detection (MPU6050)
- **Hybrid communication** — WiFi/MQTT when online, Bluetooth LE when offline
- **Offline buffering** — telemetry and alarms stored on MicroSD card, auto-synced on reconnect
- **Remote configuration** — thresholds, reporting intervals, and behavior settable via MQTT or BLE
- **Multi-modal alerts** — buzzer (PWM), haptic vibration motor, and RGB LED status indicator
- **Battery monitoring** — voltage measurement via ADC voltage divider
- **OLED display** — on-device status and sensor readouts

### 🖥️ Server (Flask + MQTT)
- User registration and login with secure password hashing
- Real-time web dashboard with live telemetry and alarm history
- Remote device configuration via dedicated UI forms
- Automatic device lifecycle management (factory reset detection, owner reassignment)
- RESTful API for mobile app integration

### 📱 Mobile App (Android)
- User authentication via server API
- **BLE provisioning** — pair new devices without internet access
- **Hybrid control mode** — commands sent directly over BLE when in range, via server otherwise
- Full telemetry visualization (temperature, battery, shock events, etc.)
- Complete offline configuration over BLE (thresholds, alarm actions, intervals)

---

## 🏗️ System Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                         ESP32 Device                           │
│   BME280 ──┐                                                    │
│   MPU6050 ─┼── I2C ──► Sensor Logic ──► MQTT (WiFi)  ──────────┼──► Broker (Mosquitto)
│   OLED ────┘                        └──► BLE Peripheral ───────┼──► Mobile App
│   MicroSD ────── SPI ──► Offline Buffer ───────────────────────┘
│   Buzzer / LED / Vibration Motor (output)                       │
└─────────────────────────────────────────────────────────────────┘
                                │ MQTT
                    ┌───────────▼───────────┐
                    │    Flask Server       │
                    │  ┌─────────────────┐  │
                    │  │  Web Dashboard  │  │
                    │  │  User Accounts  │  │
                    │  │  REST API       │  │
                    │  └─────────────────┘  │
                    └───────────────────────┘
                                │ REST API
                    ┌───────────▼───────────┐
                    │    Android App        │
                    │  BLE ◄──────────────  │
                    └───────────────────────┘
```

---

## 📡 MQTT Communication

**Topic structure:** `packageguard/{userID}/{macAddress}/{message_type}`

| Topic suffix | Direction | Description |
|---|---|---|
| `.../data` | Device → Server | Periodic telemetry report |
| `.../event` | Device → Server | Immediate alarm notification |
| `.../cmd` | Server → Device | Control command or configuration update |

**Example payloads:**

```jsonc
// Telemetry (data)
{ "temp": 22.5, "hum": 45.1, "pres": 101325, "lux": 150.0,
  "bat": 3.81, "g": 1.02, "armed": true, "alarms": 5, "ts": 1674684000 }

// Alarm event
{ "type": "ALARM_SHOCK", "val": 2.5, "ts": 1674684005 }

// Command — arm/disarm
{ "set": "ARM" }

// Command — reconfigure
{ "config": { "shock_threshold_g": 0.5, "temp_max_c": 50.0, "stealth_mode_enabled": true } }
```

---

## 🔵 BLE Protocol

The ESP32 operates as a **BLE Peripheral**. The mobile app acts as Central.

| Command | Format | Description |
|---|---|---|
| Pair device | `PAIR:{userID}` | Assigns device to a user, stores in NVS |
| Arm | `ARM` | Activates alarm monitoring |
| Disarm | `DISARM` | Deactivates alarm monitoring |
| Configure | `CFG:{...JSON...}` | Full configuration update over BLE |
| Status read | — | Returns current sensor snapshot |

BLE is used in two distinct operational modes:
1. **Provisioning** — initial pairing on factory reset, no WiFi credentials required
2. **Offline control** — direct device management when out of WiFi range (e.g., in transit, warehouse)

---

## 🔌 Hardware Components

| Component | Function | Interface |
|---|---|---|
| ESP32 | Microcontroller | — |
| MPU6050 | Accelerometer + Gyroscope (shock detection) | I2C |
| BME280 | Temperature, humidity, pressure | I2C |
| OLED Display | Status UI | I2C |
| MicroSD Reader | Offline data buffer, logs | SPI |
| RGB LED | Status indicator | RMT (GPIO) |
| Buzzer | Audible alarm | PWM (GPIO) |
| Vibration Motor | Haptic alarm | GPIO |
| Push Button | Pairing mode / factory reset | GPIO |
| Voltage Divider | Li-Ion/Li-Po battery level via ADC | ADC |

---

## 🚀 Getting Started

### Prerequisites

- ESP32 development board
- Arduino IDE or PlatformIO
- Python 3.x + pip
- MQTT broker (e.g., [Mosquitto](https://mosquitto.org/))
- Android Studio (for mobile app)

### 1. Flash the Device

```bash
# Clone the repository
git clone https://github.com/yourusername/packageguard-pro.git
cd packageguard-pro/firmware

# Open in Arduino IDE or PlatformIO and configure:
# - WiFi SSID/password
# - MQTT broker IP
# - Sensor thresholds (or leave defaults)

# Flash to ESP32
```

### 2. Run the Server

```bash
cd server
pip install -r requirements.txt

# Configure broker address in config.py
python app.py
```

Dashboard available at `http://localhost:5000`

### 3. Install the Mobile App

```bash
cd mobile
# Open in Android Studio
# Set server IP in NetworkConfig.kt (or equivalent)
# Build & run on device
```

### 4. Pair Your Device

1. Hold the button on the device to enter pairing mode
2. Open the mobile app → "Add Device"
3. Select your device from the BLE scan list
4. The app sends `PAIR:{userID}` — device is now registered

---

## 📁 Repository Structure

```
packageguard-pro/
├── firmware/          # ESP32 source code (Arduino/C++)
│   ├── src/
│   └── platformio.ini
├── server/            # Flask backend + MQTT client
│   ├── app.py
│   ├── requirements.txt
│   └── templates/
├── mobile/            # Android application
│   └── app/
└── docs/              # Additional documentation, diagrams
```

---

## 🎓 Academic Context

This project was developed as part of the **Internet of Things** course at:

**AGH University of Science and Technology, Kraków**  
Faculty of Computer Science, Electronics and Telecommunications  
Semester 5 — Intelligent Systems programme

---

## 📄 License

This project is released for educational and portfolio purposes.  
See [LICENSE](LICENSE) for details.
