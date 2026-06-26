#include <Arduino.h>
#include <DHT.h>
#include <math.h>
#include <string.h>

// =============================================================================
// SMART OFFICE ENERGY MANAGEMENT SYSTEM — STM32F411 Firmware
// Fixed version:
// - Correct float formatting using dtostrf()
// - Static JSON buffer
// - seq + ts + CRC16
// - Non-blocking ACK/NACK retry
// - PIR debounce
// - Override timeout
// - UART statistics
// =============================================================================

// --- Pin Definitions ---------------------------------------------------------
#define DHTPIN        PA1
#define DHTTYPE       DHT22
#define PIR_PIN       PA2
#define RELAY_FAN     PA3
#define RELAY_LIGHT   PA4
#define ACS_PIN_FAN   PA0
#define ACS_PIN_LIGHT PA5

// --- UART --------------------------------------------------------------------
#define ESP_UART Serial1
#define DEBUG_SERIAL Serial

const unsigned long UART_BAUD_RATE = 115200;

// --- ACS712 Calibration Constants --------------------------------------------
const float VREF        = 3.3;

// Use 0.185 for ACS712 5A.
// Use 0.100 for ACS712 20A.
// Use 0.066 for ACS712 30A.
const float SENSITIVITY = 0.185;

const float ADC_SCALE = 4095.0;

float ZERO_VOLTAGE_FAN   = 0.0;
float ZERO_VOLTAGE_LIGHT = 0.0;

// --- Timing ------------------------------------------------------------------
const unsigned long PIR_DEBOUNCE_MS       = 1000;        // 1 second practical debounce
const unsigned long OCCUPANCY_TIMEOUT_MS  = 900000UL;    // 15 minutes
const unsigned long OVERRIDE_TIMEOUT_MS   = 1800000UL;   // 30 minutes
const unsigned long PUBLISH_INTERVAL_MS   = 2300;
const unsigned long ACK_TIMEOUT_MS        = 1000;

// --- Automation State Variables ----------------------------------------------
unsigned long lastMotionTime = 0;
bool roomOccupied = false;
bool fanIsOn      = false;
bool lightIsOn    = false;

// --- PIR Debounce Variables ---------------------------------------------------
unsigned long pirHighStartTime = 0;
bool pirCandidateHigh = false;

// --- Override State Machine ---------------------------------------------------
enum Mode { AUTO, FORCE_ON, FORCE_OFF };
Mode fanMode   = AUTO;
Mode lightMode = AUTO;
unsigned long overrideStartTime = 0;

// --- Last Valid DHT22 Readings ------------------------------------------------
float lastValidT = 24.0;
float lastValidH = 75.0;

DHT dht(DHTPIN, DHTTYPE);

// --- Latest Power Values ------------------------------------------------------
float fanAmps    = 0.0;
float lightAmps  = 0.0;
float fanPower   = 0.0;
float lightPower = 0.0;

// --- Static Telemetry Buffer --------------------------------------------------
char telemetryPayload[512];
char pendingPayload[512];
uint32_t seqCounter = 0;
uint32_t pendingSeq = 0;

// --- UART ACK/NACK State ------------------------------------------------------
enum UartState { UART_IDLE, UART_WAIT_ACK, UART_RETRY_PENDING };
UartState uartState = UART_IDLE;

unsigned long lastPublishTime = 0;
unsigned long lastUartSendTime = 0;
uint8_t retryCount = 0;
const uint8_t MAX_RETRY = 2;

// --- UART Statistics for Chapter 6 -------------------------------------------
uint32_t ackCount = 0;
uint32_t nackCount = 0;
uint32_t timeoutCount = 0;
uint32_t retransmissionCount = 0;

// --- UART RX Buffers ----------------------------------------------------------
char commandBuffer[40];
size_t commandIndex = 0;
bool readingCommand = false;

char lineBuffer[40];
size_t lineIndex = 0;

// =============================================================================
// CRC16
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
// FLOAT TO CHAR HELPER
// This fixes STM32 snprintf() float issue.
// =============================================================================
void floatToChar(float value, char* buffer, size_t bufferSize, uint8_t decimals) {
  dtostrf(value, 0, decimals, buffer);

  // Remove leading spaces from dtostrf()
  char* start = buffer;
  while (*start == ' ') {
    start++;
  }

  if (start != buffer) {
    memmove(buffer, start, strlen(start) + 1);
  }
}

// =============================================================================
// COMMAND HANDLING
// =============================================================================
void applyCommand(const char* command) {
  bool overrideCommand = false;

  if (strcmp(command, "FAN_ON") == 0) {
    fanMode = FORCE_ON;
    overrideCommand = true;
  } 
  else if (strcmp(command, "FAN_OFF") == 0) {
    fanMode = FORCE_OFF;
    overrideCommand = true;
  } 
  else if (strcmp(command, "LIGHT_ON") == 0) {
    lightMode = FORCE_ON;
    overrideCommand = true;
  } 
  else if (strcmp(command, "LIGHT_OFF") == 0) {
    lightMode = FORCE_OFF;
    overrideCommand = true;
  } 
  else if (strcmp(command, "AUTO") == 0) {
    fanMode = AUTO;
    lightMode = AUTO;
    overrideStartTime = 0;
  }

  if (overrideCommand) {
    overrideStartTime = millis();
  }

  DEBUG_SERIAL.print("[COMMAND RX] ");
  DEBUG_SERIAL.println(command);
}

// Reads both:
// 1. ESP commands: <FAN_ON>, <FAN_OFF>, <LIGHT_ON>, <LIGHT_OFF>, <AUTO>
// 2. ESP ACK/NACK lines: ACK:12 or NACK:12
void readEspUart() {
  while (ESP_UART.available()) {
    char c = (char)ESP_UART.read();

    // Command parser: <COMMAND>
    if (c == '<') {
      readingCommand = true;
      commandIndex = 0;
      continue;
    }

    if (readingCommand) {
      if (c == '>') {
        commandBuffer[commandIndex] = '\0';
        applyCommand(commandBuffer);
        readingCommand = false;
        commandIndex = 0;
      } 
      else if (commandIndex < sizeof(commandBuffer) - 1) {
        commandBuffer[commandIndex++] = c;
      } 
      else {
        readingCommand = false;
        commandIndex = 0;
      }

      continue;
    }

    // ACK/NACK line parser
    if (c == '\r') {
      continue;
    }

    if (c == '\n') {
      lineBuffer[lineIndex] = '\0';

      if (lineIndex > 0 && uartState == UART_WAIT_ACK) {
        char expectedAck[24];
        char expectedNack[24];

        snprintf(expectedAck, sizeof(expectedAck), "ACK:%lu", (unsigned long)pendingSeq);
        snprintf(expectedNack, sizeof(expectedNack), "NACK:%lu", (unsigned long)pendingSeq);

        if (strcmp(lineBuffer, expectedAck) == 0) {
          ackCount++;
          retryCount = 0;
          uartState = UART_IDLE;
        } 
        else if (strcmp(lineBuffer, expectedNack) == 0) {
          nackCount++;
          uartState = UART_RETRY_PENDING;
        }
      }

      lineIndex = 0;
    } 
    else {
      if (lineIndex < sizeof(lineBuffer) - 1) {
        lineBuffer[lineIndex++] = c;
      } 
      else {
        lineIndex = 0;
      }
    }
  }
}

// =============================================================================
// SENSOR AND CONTROL LOGIC
// =============================================================================
void updateOccupancyState() {
  int motionDetected = digitalRead(PIR_PIN);
  unsigned long now = millis();

  if (motionDetected == HIGH) {
    if (!pirCandidateHigh) {
      pirCandidateHigh = true;
      pirHighStartTime = now;
    }

    if ((now - pirHighStartTime) >= PIR_DEBOUNCE_MS) {
      roomOccupied = true;
      lastMotionTime = now;
    }
  } 
  else {
    pirCandidateHigh = false;

    if (roomOccupied && ((now - lastMotionTime) > OCCUPANCY_TIMEOUT_MS)) {
      roomOccupied = false;
    }
  }

  // Debug print every 1 second only
  static unsigned long lastPirDebug = 0;
  if (millis() - lastPirDebug >= 1000) {
    lastPirDebug = millis();

    DEBUG_SERIAL.print("PIR=");
    DEBUG_SERIAL.print(motionDetected);
    DEBUG_SERIAL.print(" occupied=");
    DEBUG_SERIAL.print(roomOccupied);
    DEBUG_SERIAL.print(" temp=");
    DEBUG_SERIAL.println(lastValidT);
  }
}

void updateOverrideTimeout() {
  if (fanMode != AUTO || lightMode != AUTO) {
    if (overrideStartTime == 0) {
      overrideStartTime = millis();
    }

    if ((millis() - overrideStartTime) >= OVERRIDE_TIMEOUT_MS) {
      fanMode = AUTO;
      lightMode = AUTO;
      overrideStartTime = 0;
      DEBUG_SERIAL.println("[OVERRIDE] Timeout expired. Returning to AUTO.");
    }
  }
}

void updateActuationLogic() {
  bool autoLight = roomOccupied;
  bool autoFan   = fanIsOn;

  if (roomOccupied) {
    if (lastValidT > 25.0) {
      autoFan = true;
    } 
    else if (lastValidT < 21.0) {
      autoFan = false;
    }
  } 
  else {
    autoFan = false;
  }

  if (lightMode == FORCE_ON) {
    lightIsOn = true;
  } 
  else if (lightMode == FORCE_OFF) {
    lightIsOn = false;
  } 
  else {
    lightIsOn = autoLight;
  }

  if (fanMode == FORCE_ON) {
    fanIsOn = true;
  } 
  else if (fanMode == FORCE_OFF) {
    fanIsOn = false;
  } 
  else {
    fanIsOn = autoFan;
  }

  // Active-LOW relay module: LOW = ON, HIGH = OFF
  digitalWrite(RELAY_LIGHT, lightIsOn ? LOW : HIGH);
  digitalWrite(RELAY_FAN,   fanIsOn   ? LOW : HIGH);
}

float readCurrent(uint8_t pin, float zeroVoltage, bool loadOn) {
  long rawSum = 0;

  for (int i = 0; i < 50; i++) {
    rawSum += analogRead(pin);
    delay(1);
  }

  float rawAverage = rawSum / 50.0;
  float voltage = (rawAverage / ADC_SCALE) * VREF;
  float current = (voltage - zeroVoltage) / SENSITIVITY;

  current = fabs(current);

  if (current < 0.10) {
    current = 0.0;
  }

  if (!loadOn) {
    current = 0.0;
  }

  return current;
}

void updateCurrentAndPower() {
  fanAmps = readCurrent(ACS_PIN_FAN, ZERO_VOLTAGE_FAN, fanIsOn);
  lightAmps = readCurrent(ACS_PIN_LIGHT, ZERO_VOLTAGE_LIGHT, lightIsOn);

  fanPower = fanAmps * 5.0;
  lightPower = lightAmps * 5.0;
}

// =============================================================================
// TELEMETRY FORMAT
// =============================================================================
bool formatTelemetryPayload() {
  uint32_t seq = ++seqCounter;

  char tempStr[12];
  char humStr[12];
  char fanAmpsStr[12];
  char lightAmpsStr[12];
  char fanPowerStr[12];
  char lightPowerStr[12];

  floatToChar(lastValidT, tempStr, sizeof(tempStr), 1);
  floatToChar(lastValidH, humStr, sizeof(humStr), 1);
  floatToChar(fanAmps, fanAmpsStr, sizeof(fanAmpsStr), 2);
  floatToChar(lightAmps, lightAmpsStr, sizeof(lightAmpsStr), 2);
  floatToChar(fanPower, fanPowerStr, sizeof(fanPowerStr), 2);
  floatToChar(lightPower, lightPowerStr, sizeof(lightPowerStr), 2);

  int len = snprintf(
    telemetryPayload,
    sizeof(telemetryPayload),
    "{\"seq\":%lu,\"ts\":%lu,"
    "\"Temperature\":%s,"
    "\"Humidity\":%s,"
    "\"Fan_Amps\":%s,"
    "\"Light_Amps\":%s,"
    "\"Fan_Power\":%s,"
    "\"Light_Power\":%s,"
    "\"Occupancy\":\"%s\","
    "\"Fan_Status\":\"%s\","
    "\"Light_Status\":\"%s\"",
    (unsigned long)seq,
    (unsigned long)millis(),
    tempStr,
    humStr,
    fanAmpsStr,
    lightAmpsStr,
    fanPowerStr,
    lightPowerStr,
    roomOccupied ? "ON" : "OFF",
    fanIsOn ? "ON" : "OFF",
    lightIsOn ? "ON" : "OFF"
  );

  if (len <= 0 || len >= (int)sizeof(telemetryPayload)) {
    return false;
  }

  uint16_t crc = crc16((const uint8_t*)telemetryPayload, len);

  int finalLen = snprintf(
    telemetryPayload + len,
    sizeof(telemetryPayload) - len,
    ",\"crc\":\"0x%04X\"}",
    crc
  );

  if (finalLen <= 0 || (len + finalLen) >= (int)sizeof(telemetryPayload)) {
    return false;
  }

  pendingSeq = seq;
  return true;
}

// =============================================================================
// UART TELEMETRY SEND / RETRY
// =============================================================================
void sendTelemetryPayload() {
  strncpy(pendingPayload, telemetryPayload, sizeof(pendingPayload) - 1);
  pendingPayload[sizeof(pendingPayload) - 1] = '\0';

  ESP_UART.println(pendingPayload);

  lastUartSendTime = millis();
  retryCount = 0;
  uartState = UART_WAIT_ACK;

  DEBUG_SERIAL.print("[UART TX] seq=");
  DEBUG_SERIAL.print(pendingSeq);
  DEBUG_SERIAL.print(" size=");
  DEBUG_SERIAL.println(strlen(pendingPayload));
}

void handleUartRetry() {
  if (uartState == UART_WAIT_ACK) {
    if ((millis() - lastUartSendTime) >= ACK_TIMEOUT_MS) {
      timeoutCount++;
      uartState = UART_RETRY_PENDING;
    }
  }

  if (uartState == UART_RETRY_PENDING) {
    if (retryCount < MAX_RETRY) {
      retryCount++;
      retransmissionCount++;

      ESP_UART.println(pendingPayload);
      lastUartSendTime = millis();
      uartState = UART_WAIT_ACK;

      DEBUG_SERIAL.print("[UART RETRY] seq=");
      DEBUG_SERIAL.print(pendingSeq);
      DEBUG_SERIAL.print(" retry=");
      DEBUG_SERIAL.println(retryCount);
    } 
    else {
      DEBUG_SERIAL.print("[UART DROP] seq=");
      DEBUG_SERIAL.println(pendingSeq);

      retryCount = 0;
      uartState = UART_IDLE;
    }
  }
}

void telemetryTask() {
  if ((millis() - lastPublishTime) < PUBLISH_INTERVAL_MS) {
    return;
  }

  // If ACK is not received yet, wait until retry/drop finishes.
  if (uartState != UART_IDLE) {
    return;
  }

  lastPublishTime = millis();

  updateCurrentAndPower();

  if (formatTelemetryPayload()) {
    DEBUG_SERIAL.print("TX: ");
    DEBUG_SERIAL.println(telemetryPayload);
    sendTelemetryPayload();
  } 
  else {
    DEBUG_SERIAL.println("[ERROR] Telemetry format failed.");
  }
}

// =============================================================================
// SETUP
// =============================================================================
void setup() {
  DEBUG_SERIAL.begin(115200);
  ESP_UART.begin(UART_BAUD_RATE);

  analogReadResolution(12);
  dht.begin();

  pinMode(PIR_PIN, INPUT);
  pinMode(ACS_PIN_FAN, INPUT_ANALOG);
  pinMode(ACS_PIN_LIGHT, INPUT_ANALOG);
  pinMode(RELAY_FAN, OUTPUT);
  pinMode(RELAY_LIGHT, OUTPUT);

  // Active-LOW relay: HIGH = OFF
  digitalWrite(RELAY_FAN, HIGH);
  digitalWrite(RELAY_LIGHT, HIGH);

  delay(500);

  DEBUG_SERIAL.println("=== STM32 Smart Office Firmware ===");
  DEBUG_SERIAL.println("Calibrating ACS712 zero points...");

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

  DEBUG_SERIAL.println("Boot complete. Starting main loop.");
}

// =============================================================================
// MAIN LOOP
// =============================================================================
void loop() {
  readEspUart();
  handleUartRetry();

  float t = dht.readTemperature();
  float h = dht.readHumidity();

  if (!isnan(t)) {
    lastValidT = t;
  }

  if (!isnan(h)) {
    lastValidH = h;
  }

  updateOccupancyState();
  updateOverrideTimeout();
  updateActuationLogic();

  telemetryTask();

  static unsigned long lastStatsPrint = 0;
  if (millis() - lastStatsPrint >= 30000) {
    lastStatsPrint = millis();

    DEBUG_SERIAL.print("[UART STATS] ACK=");
    DEBUG_SERIAL.print(ackCount);
    DEBUG_SERIAL.print(" NACK=");
    DEBUG_SERIAL.print(nackCount);
    DEBUG_SERIAL.print(" TIMEOUT=");
    DEBUG_SERIAL.print(timeoutCount);
    DEBUG_SERIAL.print(" RETRY=");
    DEBUG_SERIAL.println(retransmissionCount);
  }
}



