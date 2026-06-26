IoT-Based Smart Office Energy Management System 🏢⚡

Firmware and cloud configuration for an undergraduate thesis project:

Development of an IoT-Based Smart Office Energy Management System with Authenticated MQTT Communication Using STM32–ESP8266 Embedded Prototype

This project implements a single-zone, low-voltage 5 V DC smart office prototype using STM32F411, ESP8266, AWS IoT Core, Node-RED, InfluxDB, and Grafana.

⸻

Project Structure

.
├── STM32_Blackpill/
│   └── STM32 firmware for sensor reading, automation logic, relay control, and UART telemetry
│
├── ESP8266_Gateway/
│   └── ESP8266 firmware for Wi-Fi, MQTT over TLS, mTLS, AWS IoT Core, and offline fallback dashboard
│
├── Node-RED_Flows/
│   └── Exported Node-RED flows for MQTT routing, InfluxDB storage, and scheduling
│
└── README.md

⸻

Main Features

* STM32F411 local edge automation
* PIR occupancy detection with 15-minute timeout
* DHT22 temperature hysteresis control
* ACS712 current estimation
* Relay control for 5 V DC LED and fan loads
* ESP8266 MQTT gateway
* MQTT over TLS with AWS IoT Core
* mTLS certificate-based authentication
* CRC16 UART validation with ACK/NACK handling
* Node-RED routing to InfluxDB
* Grafana dashboard visualization
* Offline fallback Access Point and local control dashboard

⸻

Security Note

AWS IoT certificates, private keys, endpoints, and Wi-Fi credentials are not included in this repository.

To run the ESP8266 firmware:

1. Go to the ESP8266_Gateway/ directory.
2. Rename:

secrets_template.h

to:

secrets.h

3. Add your own:

Wi-Fi SSID
Wi-Fi password
AWS IoT endpoint
Root CA certificate
Device certificate
Private key
MQTT topics

4. Compile and flash using PlatformIO or Arduino IDE.

⸻

Prototype Limitation

This project is a 5 V DC laboratory prototype. It is not designed for direct 220 V AC office deployment.

Real AC deployment would require proper AC metering, certified switching components, electrical protection, enclosure design, and safety certification.

⸻

Tools Used

* STM32F411 Black Pill
* ESP8266
* PlatformIO / Arduino Framework
* AWS IoT Core
* Node-RED
* InfluxDB
* Grafana
* Wireshark
* Mosquitto

⸻

Author

Dinan Hifzhan Azizi
Electronics and Instrumentation Study Program
Universitas Gadjah Mada
2026
