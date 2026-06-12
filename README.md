# Secure IoT Smart Office Energy Management System 🏢⚡️

This repository contains the firmware and cloud routing configurations for my undergraduate thesis: *Development of a Secure IoT-Based Smart Office Energy Management System Using STM32–ESP8266 Embedded Prototype*.

## 🏗 System Architecture
* **`STM32_Blackpill/` (Edge Controller):** Executes deterministic, real-time local automation. Handles PIR occupancy timeout (15 min), DHT22 temperature hysteresis (21°C–25°C), and ACS712 multi-sample current averaging.
* **`ESP8266_Gateway/` (Secure Communication):** Acts as the networking bridge. Manages Wi-Fi reconnections, hosts the offline HTML fallback dashboard, and handles all Mutual TLS (mTLS) cryptographic handshaking with AWS IoT Core.
* **`Node-RED_Flows/` (Cloud Middleware):** Contains the exported JSON flows used to route MQTT telemetry to a local InfluxDB and execute predictive CRON-based facility scheduling.

## 🔒 Security Note (How to Run)
For security purposes, the AWS IoT Core X.509 device certificates, private keys, and Wi-Fi credentials have been removed from this repository. 

To replicate this project:
1. Navigate to the `ESP8266_Gateway` directory.
2. Rename `secrets_template.h` to `secrets.h`.
3. Paste your own Wi-Fi credentials and AWS certificates into the designated variables.
4. Compile and flash using PlatformIO or the Arduino IDE.
