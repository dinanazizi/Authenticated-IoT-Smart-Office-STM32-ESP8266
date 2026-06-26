# IoT-Based Smart Office Energy Management System

Firmware and cloud configuration for the undergraduate thesis project:

> **Development of an IoT-Based Smart Office Energy Management System with Authenticated MQTT Communication Using STM32–ESP8266 Embedded Prototype**

This repository contains a **single-zone, low-voltage 5 V DC smart office prototype** using STM32F411, ESP8266, AWS IoT Core, Node-RED, InfluxDB, and Grafana.

---

## 📁 Repository Structure

```text
.
├── STM32_Blackpill/
│   └── Firmware for sensor reading, edge automation, relay control, and UART telemetry
│
├── ESP8266_Gateway/
│   └── Firmware for Wi-Fi, MQTT over TLS, mTLS, AWS IoT Core, and offline fallback mode
│
├── Node-RED_Flows/
│   └── Exported Node-RED flows for MQTT routing, InfluxDB storage, and scheduling
│
└── README.md
```

---

## ✨ Main Features

| Module | Features |
|---|---|
| **STM32F411 Edge Controller** | PIR occupancy detection, 15-minute timeout, DHT22 hysteresis control, ACS712 current estimation, relay control |
| **ESP8266 Gateway** | Wi-Fi connection, MQTT over TLS, mTLS authentication, AWS IoT Core integration, offline fallback dashboard |
| **UART Communication** | JSON telemetry, CRC16 validation, ACK/NACK handling, retry mechanism |
| **Cloud & Dashboard** | Node-RED routing, InfluxDB storage, Grafana visualization, scheduled command publishing |

---

## 🔐 Security Note

Sensitive credentials are **not included** in this repository, including:

- Wi-Fi SSID and password
- AWS IoT Core endpoint
- Root CA certificate
- X.509 device certificate
- Device private key
- MQTT topic configuration

To run the ESP8266 firmware, create your own secret configuration file.

```text
ESP8266_Gateway/
├── secrets_template.h
└── secrets.h
```

Rename:

```text
secrets_template.h
```

to:

```text
secrets.h
```

Then fill in your own Wi-Fi and AWS IoT Core credentials.

---

## 🚀 How to Run

### 1. Flash STM32 firmware

Open the `STM32_Blackpill/` directory using PlatformIO or Arduino-compatible STM32 setup, then upload the firmware to the STM32F411 Black Pill.

### 2. Configure ESP8266 credentials

Open the `ESP8266_Gateway/` directory and create `secrets.h` from `secrets_template.h`.

Add your own:

```text
Wi-Fi credentials
AWS IoT endpoint
Root CA certificate
Device certificate
Private key
MQTT topics
```

### 3. Flash ESP8266 firmware

Compile and upload the ESP8266 gateway firmware using PlatformIO or Arduino IDE.

### 4. Import Node-RED flows

Import the exported flow file from:

```text
Node-RED_Flows/
```

Configure the MQTT, AWS IoT, and InfluxDB nodes according to your environment.

### 5. Open Grafana dashboard

Connect Grafana to InfluxDB and create panels for telemetry visualization, relay states, energy estimation, and billing estimation.

---

## ⚠️ Prototype Limitation

This project is designed only as a **5 V DC laboratory prototype**.

It is **not** intended for direct 220 V AC office deployment.

Real AC deployment requires:

- Dedicated AC energy metering
- Certified relay or contactor
- Fuse and protection circuit
- Proper enclosure design
- Electrical safety certification

---

## 🛠 Tools and Platforms

| Category | Tools |
|---|---|
| Microcontroller | STM32F411 Black Pill, ESP8266 |
| Firmware | PlatformIO, Arduino Framework |
| Cloud & Middleware | AWS IoT Core, Node-RED |
| Database & Dashboard | InfluxDB, Grafana |
| Validation | Wireshark, Mosquitto, OpenSSL |

---
