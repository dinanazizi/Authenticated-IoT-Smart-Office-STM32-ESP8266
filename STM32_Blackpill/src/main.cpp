#include <Arduino.h>
#include <DHT.h>

// =============================================================================
//  SMART OFFICE ENERGY MANAGEMENT SYSTEM — STM32F411 Firmware
//  Includes: Offline Manual Override via UART + Debounce + Hysteresis
// =============================================================================

// --- Pin Definitions ---------------------------------------------------------
#define DHTPIN        PA1
#define DHTTYPE       DHT22
#define PIR_PIN       PA2
#define RELAY_FAN     PA3
#define RELAY_LIGHT   PA4
#define ACS_PIN_FAN   PA0    
#define ACS_PIN_LIGHT PA5    

// --- ACS712 Calibration Constants --------------------------------------------
const float VREF        = 3.3;    
const float SENSITIVITY = 0.185;  
const float ADC_SCALE   = 4095.0; 

float ZERO_VOLTAGE_FAN   = 0.0;
float ZERO_VOLTAGE_LIGHT = 0.0;

// --- Automation State Variables ----------------------------------------------
unsigned long lastMotionTime = 0;
bool roomOccupied = false;
bool fanIsOn      = false;
bool lightIsOn    = false;

// --- NEW: Override State Machine for Offline Control -------------------------
enum Mode { AUTO, FORCE_ON, FORCE_OFF };
Mode fanMode   = AUTO;
Mode lightMode = AUTO;

// --- Last Valid DHT22 Readings -----------------------------------------------
float lastValidT = 24.0;
float lastValidH = 75.0;

DHT dht(DHTPIN, DHTTYPE);

// =============================================================================
//  NEW: OFFLINE COMMAND LISTENER
// =============================================================================
void checkOfflineManualOverride() {
  // Read any incoming data from ESP8266 on PA10 (RX)
  while (Serial1.available()) {
    String incoming = Serial1.readStringUntil('>'); 
    int startIndex = incoming.indexOf('<'); 
    
    if (startIndex >= 0) {
      String command = incoming.substring(startIndex + 1);
      
      // Update the Override State Machine
      if (command == "FAN_ON")         fanMode = FORCE_ON;
      else if (command == "FAN_OFF")   fanMode = FORCE_OFF;
      else if (command == "LIGHT_ON")  lightMode = FORCE_ON;
      else if (command == "LIGHT_OFF") lightMode = FORCE_OFF;
      else if (command == "AUTO") {
        fanMode   = AUTO;
        lightMode = AUTO;
      }
      
      if (Serial) {
        Serial.println("\n[OFFLINE OVERRIDE RX] Command applied: " + command);
      }
    }
  }
}

// =============================================================================
//  SETUP
// =============================================================================
void setup() {
  Serial.begin(115200);
  Serial1.begin(115200); // UART to ESP8266

  analogReadResolution(12);
  dht.begin();

  pinMode(PIR_PIN,     INPUT);
  pinMode(ACS_PIN_FAN,   INPUT_ANALOG);
  pinMode(ACS_PIN_LIGHT, INPUT_ANALOG);
  pinMode(RELAY_FAN,   OUTPUT);
  pinMode(RELAY_LIGHT, OUTPUT);

  // Relays are active-LOW: HIGH = OFF, LOW = ON
  digitalWrite(RELAY_FAN,   HIGH);
  digitalWrite(RELAY_LIGHT, HIGH);
  delay(500);

  if (Serial) Serial.println("=== STM32 Smart Office Firmware ===");
  if (Serial) Serial.println("Calibrating ACS712 zero points...");

  long zeroSumFan = 0;
  for (int i = 0; i < 500; i++) {
    zeroSumFan += analogRead(ACS_PIN_FAN);
    delay(2);
  }
  ZERO_VOLTAGE_FAN = ((zeroSumFan / 500.0) / ADC_SCALE) * VREF;

  long zeroSumLight = 0;
  for (int i = 0; i < 500; i++) {
    zeroSumLight += analogRead(ACS_PIN_LIGHT);
    delay(2);
  }
  ZERO_VOLTAGE_LIGHT = ((zeroSumLight / 500.0) / ADC_SCALE) * VREF;

  if (Serial) Serial.println("Boot complete. Starting main loop.");
}

// =============================================================================
//  MAIN LOOP
// =============================================================================
void loop() {

  // --- 0. Check for Manual ESP8266 Commands ---
  checkOfflineManualOverride();

  // --- 1. Read Sensors ---
  float t = dht.readTemperature();
  float h = dht.readHumidity();
  int   motionDetected = digitalRead(PIR_PIN);

  if (!isnan(t)) lastValidT = t;
  if (!isnan(h)) lastValidH = h;

  // --- 2. Occupancy Logic (Sensor Layer) ---
  if (motionDetected == HIGH) {
    if ((millis() - lastMotionTime) > 5000) {
      roomOccupied  = true;
      lastMotionTime = millis();  
    }
  } else {
    if (roomOccupied && ((millis() - lastMotionTime) > 900000)) {
      roomOccupied = false;
    }
  }

  // --- 3. ACTUATION LOGIC WITH OVERRIDE SUPPORT ---
  
  // A. Calculate what the SENSORS want to do (Auto Mode)
  bool autoLight = roomOccupied;
  bool autoFan   = fanIsOn; // Default to holding current state

  if (roomOccupied) {
    if (lastValidT > 25.0) autoFan = true;
    else if (lastValidT < 21.0) autoFan = false;
  } else {
    autoFan = false;
  }

  // B. Apply OVERRIDES (Web Dashboard)
  if (lightMode == FORCE_ON)       lightIsOn = true;
  else if (lightMode == FORCE_OFF) lightIsOn = false;
  else                             lightIsOn = autoLight; // Fallback to Auto

  if (fanMode == FORCE_ON)         fanIsOn = true;
  else if (fanMode == FORCE_OFF)   fanIsOn = false;
  else                             fanIsOn = autoFan;     // Fallback to Auto

  // C. Execute physical writes
  digitalWrite(RELAY_LIGHT, lightIsOn ? LOW : HIGH);
  digitalWrite(RELAY_FAN,   fanIsOn ? LOW : HIGH);


  // --- 4. Read Current (Fan) ---
  long rawFanSum = 0;
  for (int i = 0; i < 50; i++) {
    rawFanSum += analogRead(ACS_PIN_FAN);
    delay(1);
  }
  float rawFan     = rawFanSum / 50.0;
  float voltageFan = (rawFan / ADC_SCALE) * VREF;
  float fanAmps    = (voltageFan - ZERO_VOLTAGE_FAN) / SENSITIVITY;
  
  fanAmps = (abs(fanAmps) < 0.10) ? 0.0 : abs(fanAmps);
  fanAmps = fanIsOn ? fanAmps : 0.0;

  // --- 5. Read Current (Light) ---
  long rawLightSum = 0;
  for (int i = 0; i < 50; i++) {
    rawLightSum += analogRead(ACS_PIN_LIGHT);
    delay(1);
  }
  float rawLight     = rawLightSum / 50.0;
  float voltageLight = (rawLight / ADC_SCALE) * VREF;
  float lightAmps    = (voltageLight - ZERO_VOLTAGE_LIGHT) / SENSITIVITY;

  lightAmps = (abs(lightAmps) < 0.10) ? 0.0 : abs(lightAmps);
  lightAmps = lightIsOn ? lightAmps : 0.0;

  float fanPower   = fanAmps   * 5.0;
  float lightPower = lightAmps * 5.0;

  // --- 6. Format JSON Payload ---
  String tinyJson = "{";
  tinyJson += "\"Temperature\":"  + String(lastValidT,  1) + ",";
  tinyJson += "\"Humidity\":"     + String(lastValidH,  1) + ",";
  tinyJson += "\"Fan_Amps\":"     + String(fanAmps,     2) + ",";
  tinyJson += "\"Light_Amps\":"   + String(lightAmps,   2) + ",";
  tinyJson += "\"Fan_Power\":"    + String(fanPower,     2) + ",";
  tinyJson += "\"Light_Power\":"  + String(lightPower,   2) + ",";
  tinyJson += "\"Occupancy\":\""  + String(roomOccupied ? "ON" : "OFF") + "\",";
  tinyJson += "\"Fan_Status\":\""   + String(fanIsOn   ? "ON" : "OFF") + "\",";
  tinyJson += "\"Light_Status\":\"" + String(lightIsOn ? "ON" : "OFF") + "\"";
  tinyJson += "}";

  // --- 7. Transmit ---
  if (Serial) Serial.println("TX: " + tinyJson);
  
  Serial1.println();
  delay(50);
  Serial1.println(tinyJson);

  delay(2000); // Main loop delay
