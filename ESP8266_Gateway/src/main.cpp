#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <time.h>
#include <ESPAsyncTCP.h>       
#include <ESPAsyncWebServer.h> 
#include "secrets.h"

// ===================================================
// 1. SECURE CLIENT & OFFLINE SERVER OBJECTS
// ===================================================
X509List ca(root_ca);
X509List cert(device_cert);
PrivateKey key(device_key);

WiFiClientSecure espClient;
PubSubClient client(espClient);

AsyncWebServer server(80);
bool isOfflineMode = false;

// --- SECURITY CREDENTIALS FOR OFFLINE PORTAL ---
const char* http_username = "admin";
const char* http_password = "thesis_password"; // CHANGE THIS BEFORE YOUR DEFENSE!

unsigned long lastMqttReconnectAttempt = 0;
unsigned long lastWiFiReconnectAttempt = 0;

// ===================================================
// 2. MODERN OFFLINE HTML DASHBOARD (Pure CSS)
// ===================================================
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html>
<head>
  <title>Smart Office Hub</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    body { font-family: 'Segoe UI', Roboto, Helvetica, Arial, sans-serif; background-color: #e9ecef; margin: 0; display: flex; justify-content: center; align-items: center; min-height: 100vh; }
    .card { background: white; padding: 30px; border-radius: 16px; box-shadow: 0 8px 24px rgba(0,0,0,0.1); width: 90%; max-width: 400px; text-align: center; }
    h2 { color: #2c3e50; margin-bottom: 5px; font-weight: 700; }
    .status { color: #e67e22; font-size: 14px; font-weight: bold; margin-bottom: 25px; display: flex; align-items: center; justify-content: center; gap: 8px;}
    .status-dot { height: 10px; width: 10px; background-color: #e67e22; border-radius: 50%; display: inline-block; animation: blink 1.5s infinite; }
    @keyframes blink { 0% { opacity: 1; } 50% { opacity: 0.4; } 100% { opacity: 1; } }
    .device-group { border-top: 1px solid #f0f0f0; padding-top: 15px; margin-top: 15px; }
    .device-title { color: #7f8c8d; font-size: 12px; text-transform: uppercase; letter-spacing: 1px; margin-bottom: 10px; text-align: left; }
    .btn-row { display: flex; justify-content: space-between; gap: 10px; }
    button { flex: 1; padding: 14px 0; font-size: 15px; cursor: pointer; border: none; border-radius: 8px; font-weight: bold; transition: all 0.2s ease; box-shadow: 0 4px 6px rgba(0,0,0,0.05); color: white;}
    .on { background: linear-gradient(135deg, #2ecc71, #27ae60); }
    .off { background: linear-gradient(135deg, #e74c3c, #c0392b); }
    .auto { background: linear-gradient(135deg, #f39c12, #d35400); width: 100%; margin-top: 20px; padding: 16px 0; font-size: 16px; box-shadow: 0 4px 15px rgba(243, 156, 18, 0.3); }
    button:active { transform: translateY(2px); box-shadow: 0 1px 2px rgba(0,0,0,0.1); }
    #feedback { margin-top: 15px; font-size: 13px; color: #27ae60; height: 15px; font-weight: bold; }
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

// ===================================================
// 3. SETUP & UTILITY FUNCTIONS
// ===================================================

void startOfflineMode() {
  if (!isOfflineMode) {
    Serial.println("\n[FALLBACK] Starting Offline Local AP...");
    
    WiFi.mode(WIFI_AP_STA); 
    WiFi.softAP("SmartOffice_Local", "12345678"); 
    
    Serial.print("Offline Control Panel IP: ");
    Serial.println(WiFi.softAPIP());

    // --- SECURED ROUTE: HTML Dashboard ---
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
      if(!request->authenticate(http_username, http_password)) {
        return request->requestAuthentication(); 
      }
      request->send_P(200, "text/html", index_html);
    });

    // --- SECURED ROUTE: UART API ---
    server.on("/control", HTTP_GET, [](AsyncWebServerRequest *request){
      if (request->hasParam("cmd")) {
        String cmd = request->getParam("cmd")->value();
        // Send command to STM32 Black Pill via UART
        Serial.print("<" + cmd + ">"); 
      }
      request->send(200, "text/plain", "Command Sent");
    });

    server.begin();
    isOfflineMode = true;
  }
}

// THE FIX: Using Serial.print to avoid sending hidden \r\n characters
void callback(char* topic, byte* payload, unsigned int length) {
  String incomingCommand;
  incomingCommand.reserve(length); 
  
  for (unsigned int i = 0; i < length; i++) {
    incomingCommand += (char)payload[i];
  }
  
  // Forward the exact command payload directly to the STM32
  Serial.print(incomingCommand);
}

void setup_time() {
  Serial.print("Syncing time via NTP...");
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  time_t now = time(nullptr);
  int retries = 0;
  
  while (now < 100000 && retries < 40) { 
    delay(500);
    Serial.print(".");
    now = time(nullptr);
    retries++;
  }
  
  if (now < 100000) {
     Serial.println("\n[ERROR] Failed to sync time. TLS connections will fail!");
  } else {
     Serial.println("\nTime synced successfully!");
  }
}

void maintainWiFiConnection() {
  if (WiFi.status() != WL_CONNECTED) {
    unsigned long currentMillis = millis();
    
    // DELAY INCREASED TO 30s: Prevents AP choking
    if (currentMillis - lastWiFiReconnectAttempt >= 30000 || lastWiFiReconnectAttempt == 0) { 
      Serial.println("\nAttempting to reconnect to WiFi...");
      WiFi.disconnect(); // Explicitly disconnect station before retrying
      WiFi.begin(ssid, password);
      lastWiFiReconnectAttempt = currentMillis;
      
      if (!isOfflineMode) {
        startOfflineMode();
      }
    }
  } else if (lastWiFiReconnectAttempt != 0) { 
    Serial.println("\nWiFi Restored!");
    Serial.print("ESP8266 IP Address: ");
    Serial.println(WiFi.localIP());
    
    // Shut down the offline AP to save power and prevent interference
    if (isOfflineMode) {
      Serial.println("[INFO] Shutting down Local AP...");
      WiFi.softAPdisconnect(true);
      WiFi.mode(WIFI_STA); 
      isOfflineMode = false;
    }

    setup_time(); 
    lastWiFiReconnectAttempt = 0; 
  }
}

boolean reconnect() {
  if (WiFi.status() != WL_CONNECTED) return false;
  
  String clientId = "ESP8266Bridge-" + String(random(0, 1000), HEX);
  Serial.print("Attempting secure AWS connection...");
  
  if (client.connect(clientId.c_str())) {
    Serial.println(" Success! AWS MQTT Connected.");
    // Make sure this topic matches Node-RED EXACTLY!
    client.subscribe("my_secret_office_998877/commands");
    return true;
  } else {
    Serial.println("\n\n[SECURITY ALERT] AWS Connection Rejected!");
    Serial.println("Retrying in 5 seconds...\n");
    return false;
  }
}

// ===================================================
// 4. MAIN CORE
// ===================================================
void setup() {
  Serial.setRxBufferSize(256); 
  Serial.begin(115200);
  delay(2000); 

  Serial.println("\n\n--- Booting ESP8266 Edge Node ---");

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  unsigned long startAttempt = millis();
  
  Serial.print("Connecting to Router");
  while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < 15000) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected!");
    Serial.print("ESP8266 IP Address: ");
    Serial.println(WiFi.localIP());
    setup_time();
  } else {
    Serial.println("\n[WARNING] Router not found on boot!");
    startOfflineMode(); 
  }

  espClient.setTrustAnchors(&ca);
  espClient.setClientRSACert(&cert, &key);

  client.setServer(aws_endpoint, aws_port);
  client.setCallback(callback); 
  client.setBufferSize(1024); 
}

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

  if (Serial.available()) {
    String incomingData = Serial.readStringUntil('\n');
    int startIndex = incomingData.indexOf('{');
    int endIndex = incomingData.lastIndexOf('}');
    
    if (startIndex >= 0 && endIndex > startIndex) {
      String pureJson = incomingData.substring(startIndex, endIndex + 1);
      
      if (client.connected()) {
        client.publish("my_secret_office_998877/sensors", pureJson.c_str());   
      }
    }
  }
}