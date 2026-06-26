#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <time.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include "secrets.h"

// =============================================================================
// SECURE CLIENT & OFFLINE SERVER OBJECTS
// =============================================================================
X509List ca(root_ca);
X509List cert(device_cert);
PrivateKey key(device_key);

WiFiClientSecure espClient;
PubSubClient client(espClient);

AsyncWebServer server(80);
bool isOfflineMode = false;

// =============================================================================
// CONFIGURATION
// =============================================================================
const char* http_username = "admin";
const char* http_password = "thesis_password";

const char* telemetryTopic = "my_secret_office_998877/sensors";
const char* commandTopic   = "my_secret_office_998877/commands";

unsigned long lastMqttReconnectAttempt = 0;
unsigned long lastWiFiReconnectAttempt = 0;

// =============================================================================
// OFFLINE DASHBOARD HTML
// =============================================================================
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html>
<head>
  <title>Smart Office Hub</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    body {
      font-family: 'Segoe UI', Roboto, Helvetica, Arial, sans-serif;
      background-color: #e9ecef;
      margin: 0;
      display: flex;
      justify-content: center;
      align-items: center;
      min-height: 100vh;
    }
    .card {
      background: white;
      padding: 30px;
      border-radius: 16px;
      box-shadow: 0 8px 24px rgba(0,0,0,0.1);
      width: 90%;
      max-width: 400px;
      text-align: center;
    }
    h2 {
      color: #2c3e50;
      margin-bottom: 5px;
      font-weight: 700;
    }
    .status {
      color: #e67e22;
      font-size: 14px;
      font-weight: bold;
      margin-bottom: 25px;
      display: flex;
      align-items: center;
      justify-content: center;
      gap: 8px;
    }
    .status-dot {
      height: 10px;
      width: 10px;
      background-color: #e67e22;
      border-radius: 50%;
      display: inline-block;
      animation: blink 1.5s infinite;
    }
    @keyframes blink {
      0% { opacity: 1; }
      50% { opacity: 0.4; }
      100% { opacity: 1; }
    }
    .device-group {
      border-top: 1px solid #f0f0f0;
      padding-top: 15px;
      margin-top: 15px;
    }
    .device-title {
      color: #7f8c8d;
      font-size: 12px;
      text-transform: uppercase;
      letter-spacing: 1px;
      margin-bottom: 10px;
      text-align: left;
    }
    .btn-row {
      display: flex;
      justify-content: space-between;
      gap: 10px;
    }
    button {
      flex: 1;
      padding: 14px 0;
      font-size: 15px;
      cursor: pointer;
      border: none;
      border-radius: 8px;
      font-weight: bold;
      transition: all 0.2s ease;
      box-shadow: 0 4px 6px rgba(0,0,0,0.05);
      color: white;
    }
    .on {
      background: linear-gradient(135deg, #2ecc71, #27ae60);
    }
    .off {
      background: linear-gradient(135deg, #e74c3c, #c0392b);
    }
    .auto {
      background: linear-gradient(135deg, #f39c12, #d35400);
      width: 100%;
      margin-top: 20px;
      padding: 16px 0;
      font-size: 16px;
      box-shadow: 0 4px 15px rgba(243, 156, 18, 0.3);
    }
    button:active {
      transform: translateY(2px);
      box-shadow: 0 1px 2px rgba(0,0,0,0.1);
    }
    #feedback {
      margin-top: 15px;
      font-size: 13px;
      color: #27ae60;
      height: 15px;
      font-weight: bold;
    }
  </style>
</head>

<body>
  <div class="card">
    <h2>Smart Office Hub</h2>
    <div class="status"><span class="status-dot"></span> Offline Override Active</div>

    <div class="device-group">
      <div class="device-title">HVAC Circulation (Fan)</div>
      <div class="btn-row">
        <button class="on" onclick="sendCommand('FAN_ON')">Power ON</button>
        <button class="off" onclick="sendCommand('FAN_OFF')">Power OFF</button>
      </div>
    </div>

    <div class="device-group">
      <div class="device-title">Main Lighting</div>
      <div class="btn-row">
        <button class="on" onclick="sendCommand('LIGHT_ON')">Lights ON</button>
        <button class="off" onclick="sendCommand('LIGHT_OFF')">Lights OFF</button>
      </div>
    </div>

    <button class="auto" onclick="sendCommand('AUTO')">Restore Sensor Auto-Mode</button>
    <div id="feedback"></div>
  </div>

  <script>
    function sendCommand(cmd) {
      var xhr = new XMLHttpRequest();
      xhr.open("GET", "/control?cmd=" + cmd, true);
      xhr.onload = function() {
        var fb = document.getElementById('feedback');
        fb.innerText = "Command executed successfully!";
        setTimeout(function(){ fb.innerText = ""; }, 2000);
      };
      xhr.send();
    }
  </script>
</body>
</html>
)rawliteral";

// =============================================================================
// CRC16
// Must match STM32 crc16() implementation.
// =============================================================================
uint16_t crc16(const uint8_t* data, size_t len) {
  uint16_t crc = 0xFFFF;

  for (size_t i = 0; i < len; i++) {
    crc ^= data[i];

    for (int j = 0; j < 8; j++) {
      if (crc & 1) {
        crc = (crc >> 1) ^ 0xA001;
      } else {
        crc >>= 1;
      }
    }
  }

  return crc;
}

// =============================================================================
// COMMAND UTILITY
// =============================================================================
bool isAllowedCommand(const char* cmd) {
  return strcmp(cmd, "FAN_ON") == 0 ||
         strcmp(cmd, "FAN_OFF") == 0 ||
         strcmp(cmd, "LIGHT_ON") == 0 ||
         strcmp(cmd, "LIGHT_OFF") == 0 ||
         strcmp(cmd, "AUTO") == 0;
}

void sendCommandToSTM(const char* cmd) {
  if (!isAllowedCommand(cmd)) {
    return;
  }

  Serial.print("<");
  Serial.print(cmd);
  Serial.print(">");
}

// =============================================================================
// TELEMETRY PARSING HELPERS
// =============================================================================
uint32_t extractSeqFromString(const String& json) {
  int seqIndex = json.indexOf("\"seq\":");

  if (seqIndex < 0) {
    return 0;
  }

  seqIndex += 6;
  return json.substring(seqIndex).toInt();
}

bool validatePayloadCrc(const String& json) {
  int crcFieldIndex = json.indexOf(",\"crc\":\"0x");

  if (crcFieldIndex < 0) {
    return false;
  }

  int crcValueStart = crcFieldIndex + 10;
  int crcValueEnd = json.indexOf("\"", crcValueStart);

  if (crcValueEnd < 0) {
    return false;
  }

  String crcText = json.substring(crcValueStart, crcValueEnd);
  uint16_t receivedCrc = (uint16_t) strtoul(crcText.c_str(), NULL, 16);

  String payloadWithoutCrc = json.substring(0, crcFieldIndex);
  uint16_t calculatedCrc = crc16(
    (const uint8_t*)payloadWithoutCrc.c_str(),
    payloadWithoutCrc.length()
  );

  return receivedCrc == calculatedCrc;
}

// =============================================================================
// STM32 TELEMETRY READER
// Reads STM32 JSON, validates CRC, publishes to AWS, and replies ACK/NACK.
// =============================================================================
void readStmUart() {
  if (Serial.available()) {
    String incomingData = Serial.readStringUntil('\n');

    int startIndex = incomingData.indexOf('{');
    int endIndex = incomingData.lastIndexOf('}');

    if (startIndex >= 0 && endIndex > startIndex) {
      String pureJson = incomingData.substring(startIndex, endIndex + 1);

      uint32_t seq = extractSeqFromString(pureJson);

if (validatePayloadCrc(pureJson)) {
  if (client.connected()) {
    bool ok = client.publish(telemetryTopic, pureJson.c_str());

    Serial.print("[MQTT_PUBLISH] seq=");
    Serial.print(seq);
    Serial.print(" ok=");
    Serial.print(ok);
    Serial.print(" t=");
    Serial.println(millis());
  }

  Serial.print("ACK:");
  Serial.println(seq);
      } else {
        Serial.print("NACK:");
        Serial.println(seq);
      }
    }
  }
}

// =============================================================================
// OFFLINE MODE
// =============================================================================
void startOfflineMode() {
  if (isOfflineMode) {
    return;
  }

  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP("SmartOffice_Local", "12345678");

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!request->authenticate(http_username, http_password)) {
      return request->requestAuthentication();
    }

    request->send_P(200, "text/html", index_html);
  });

  server.on("/control", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!request->authenticate(http_username, http_password)) {
      return request->requestAuthentication();
    }

    if (request->hasParam("cmd")) {
      String cmdString = request->getParam("cmd")->value();

      char cmd[24];
      cmdString.toCharArray(cmd, sizeof(cmd));

      sendCommandToSTM(cmd);
    }

    request->send(200, "text/plain", "Command Sent");
  });

  server.begin();
  isOfflineMode = true;
}

// =============================================================================
// MQTT CALLBACK
// Receives AWS IoT commands and forwards them to STM32.
// =============================================================================
void callback(char* topic, byte* payload, unsigned int length) {
  char msg[128];

  if (length >= sizeof(msg)) {
    return;
  }

  memcpy(msg, payload, length);
  msg[length] = '\0';

  if (strstr(msg, "FAN_ON")) {
    sendCommandToSTM("FAN_ON");
  }
  else if (strstr(msg, "FAN_OFF")) {
    sendCommandToSTM("FAN_OFF");
  }
  else if (strstr(msg, "LIGHT_ON")) {
    sendCommandToSTM("LIGHT_ON");
  }
  else if (strstr(msg, "LIGHT_OFF")) {
    sendCommandToSTM("LIGHT_OFF");
  }
  else if (strstr(msg, "AUTO")) {
    sendCommandToSTM("AUTO");
  }
}

// =============================================================================
// TIME AND NETWORK
// =============================================================================
void setup_time() {
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");

  time_t now = time(nullptr);
  int retries = 0;

  while (now < 100000 && retries < 40) {
    delay(500);
    now = time(nullptr);
    retries++;
  }
}

void maintainWiFiConnection() {
  if (WiFi.status() != WL_CONNECTED) {
    unsigned long currentMillis = millis();

    if (currentMillis - lastWiFiReconnectAttempt >= 30000 || lastWiFiReconnectAttempt == 0) {
      WiFi.disconnect();
      WiFi.begin(ssid, password);
      lastWiFiReconnectAttempt = currentMillis;

      if (!isOfflineMode) {
        startOfflineMode();
      }
    }
  }
  else if (lastWiFiReconnectAttempt != 0) {
    if (isOfflineMode) {
      WiFi.softAPdisconnect(true);
      WiFi.mode(WIFI_STA);
      isOfflineMode = false;
    }

    setup_time();
    lastWiFiReconnectAttempt = 0;
  }
}

boolean reconnect() {
  if (WiFi.status() != WL_CONNECTED) {
    return false;
  }

  String clientId = "ESP8266Bridge-" + String(random(0, 1000), HEX);

  if (client.connect(clientId.c_str())) {
    client.subscribe(commandTopic);
    return true;
  }

  return false;
}

// =============================================================================
// SETUP
// =============================================================================
void setup() {
  Serial.setRxBufferSize(512);
  Serial.begin(115200);
  Serial.setTimeout(200);
  delay(1000);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  unsigned long startAttempt = millis();

  while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < 15000) {
    delay(500);
  }

  if (WiFi.status() == WL_CONNECTED) {
    setup_time();
  } else {
    startOfflineMode();
  }

  espClient.setTrustAnchors(&ca);
  espClient.setClientRSACert(&cert, &key);

  client.setServer(aws_endpoint, aws_port);
  client.setCallback(callback);
  client.setBufferSize(1024);
}

// =============================================================================
// LOOP
// =============================================================================
void loop() {
  maintainWiFiConnection();

  if (WiFi.status() == WL_CONNECTED) {
    if (!client.connected()) {
      unsigned long now = millis();

      if (now - lastMqttReconnectAttempt > 5000) {
        lastMqttReconnectAttempt = now;
        reconnect();
      }
    } else {
      client.loop();
    }
  }

  readStmUart();
}
