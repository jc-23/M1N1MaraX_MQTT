
/* SPDX-License-Identifier: GPL-3.0-or-later
Copyright (C) 2024 Ralf Grafe
Copyright (C) 2026 JC-23

This file is based on M1N1MaraX_MQTT by Ralf Grafe:
<https://github.com/dougie996/M1N1MaraX_MQTT>

M1N1MaraX_MQTT is partly based on MaraX-Shot-Monitor:
<https://github.com/Anlieger/MaraX-Shot-Monitor>

This firmware runs on an external ESP8266 shot timer/monitor. It reads data
from the MaraX Gicar control box.

This version adds PlatformIO support, MaraX V1 pump detection, safer serial
parsing, non-blocking MQTT reconnect handling, and local-only secrets
configuration.

M1N1MaraX_MQTT is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

M1N1MaraX_MQTT is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with M1N1MaraX_MQTT. If not, see <https://www.gnu.org/licenses/>.
*/

//Includes
#include "bitmaps.h"
#include "secrets.h"
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Arduino.h>
#include <ArduinoOTA.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <SoftwareSerial.h>
#include <Wire.h>
#include <avr/pgmspace.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------------------
// Machine configuration
// ---------------------------------------------------------------------------
// MaraX V1:
//   Keep MARA_V1 enabled. The pump state is read from a reed contact on
//   PUMP_PIN because V1 does not provide the pump state in the serial frame.
//
// MaraX V2:
//   Comment out MARA_V1. The pump state is then taken from the serial frame.
#define MARA_V1

// ---------------------------------------------------------------------------
// Pin configuration
// ---------------------------------------------------------------------------
// Mara serial receive/transmit pins. TX is configured for SoftwareSerial but
// the MaraX/Gicar RX line normally does not need to be connected.
#define D5 (14) // D5/GPIO14: ESP RX, connected to MaraX serial TX
#define D6 (12) // D6/GPIO12: ESP TX, usually not connected

#ifdef MARA_V1
#define D7 (13) // D7/GPIO13: reed contact input for pump detection
#define PUMP_PIN D7

// Set to true if your reed contact reports HIGH while the pump is running.
bool reedOpenSensor = false;
#endif

// MaraX serial data uses inverted logic.
#define INVERSE_LOGIC 1

// ---------------------------------------------------------------------------
// Display configuration
// ---------------------------------------------------------------------------
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define SCREEN_ADDRESS 0x3C // common values are 0x3C or 0x3D

// ---------------------------------------------------------------------------
// Serial/parser configuration
// ---------------------------------------------------------------------------
// Maximum expected serial frame length, including room for the string terminator.
#define BUFFER_SIZE 32

// Set to true to show fixed example values when no MaraX serial data arrives.
#define SIMULATE_MARA_RX false

// Mark the MaraX as unavailable if no serial byte arrives for this long.
const unsigned long SERIAL_TIMEOUT_MS = 1000;

// ---------------------------------------------------------------------------
// Timing configuration
// ---------------------------------------------------------------------------
#ifdef MARA_V1
// Increase this if the shot timer disappears while brewing.
// Decrease it if the shot timer stays visible too long after brewing stops.
const unsigned long V1_PUMP_OFF_DEBOUNCE_MS = 700;
#endif

// Keep the last shot time on screen after the pump stops.
const unsigned long SHOT_TIMER_HOLD_AFTER_PUMP_OFF_MS = 4000;

// The shot timer is only shown after this many seconds to avoid flashing for
// accidental short pump/reed blips.
const int SHOT_TIMER_DISPLAY_AFTER_SECONDS = 3;

// Keep the display compact if a shot runs unusually long.
const int SHOT_TIMER_MAX_SECONDS = 99;

// OLED status/animation intervals.
const unsigned long STATUS_DISPLAY_INTERVAL_MS = 1000;
const unsigned long HEAT_BLINK_INTERVAL_MS = 1000;
const int COFFEE_CUP_FIRST_FRAME = 8;

// ---------------------------------------------------------------------------
// MQTT configuration
// ---------------------------------------------------------------------------
const char MQTT_CLIENT_ID[] = "MaraX";
const unsigned long MQTT_RECONNECT_INTERVAL_MS = 5000;

// Local configuration from secrets.h. Copy secrets.example.h to secrets.h and
// adjust WiFi/MQTT values; secrets.h is intentionally ignored by Git.
const char *wifiSSID = WLAN_SSID;
const char *wifiPassword = WLAN_PASS;
const char *mqttServer = MQTT_SERVER;
const int mqttPort = MQTT_PORT;
const unsigned long mqttUpdateInterval = MQTT_UPDATE_INTERVAL * 1000UL;
const char *otaHostname = OTA_HOSTNAME;
const char *otaPassword = OTA_PASSWORD;

// Instances
WiFiClient wifi;
WiFiEventHandler gotIpEventHandler, disconnectedEventHandler;
PubSubClient mqttClient(wifi);
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
SoftwareSerial MaraRxSerial(D5, D6, INVERSE_LOGIC); // Rx, Tx, Inverse_Logic

// Runtime state shared by the display, parser, WiFi and MQTT code.
char signalLevel[16] = "0";

unsigned long lastShotSecondMillis = 0;
unsigned long shotTimerStoppedMillis = 0;
int shotSeconds = 0;

unsigned long pumpOffSinceMillis = 0;
bool shotTimerRunning = false;

unsigned long lastSerialRxMillis = 0;
char buffer[BUFFER_SIZE];
size_t bufferIndex = 0;
bool serialFrameOverflow = false;
bool maraIsOff = false;
bool hasReceivedMaraData = false;
bool otaStarted = false;
unsigned long lastHeatBlinkMillis = 0;
bool heatBlinkOn = false;
int coffeeCupFrame = COFFEE_CUP_FIRST_FRAME;

#ifdef MARA_V1
int readV1PumpState() {
  int reedValue = digitalRead(PUMP_PIN);
  digitalWrite(LED_BUILTIN, reedValue);
  return reedOpenSensor ? reedValue : !reedValue;
}

int stabilizeV1PumpState(int rawPumpState) {
  if (rawPumpState == 1) {
    pumpOffSinceMillis = 0;
    return 1;
  }

  // Reed contacts can flicker briefly while the machine vibrates. Treat short
  // OFF readings as ON so the shot timer does not disappear mid-shot.
  if (shotTimerRunning) {
    if (pumpOffSinceMillis == 0) {
      pumpOffSinceMillis = millis();
    }
    if (millis() - pumpOffSinceMillis < V1_PUMP_OFF_DEBOUNCE_MS) {
      return 1;
    }
  }

  return 0;
}
#endif

// Mara data
struct MaraData {
  bool updated = false;
  char mode[2];
  char firmware[5];
  int hxTemp;
  int steamTemp;
  int targetSteamTemp;
  int boostCountdown;
  int heatState;
  int pumpState;
};

unsigned long lastMsg = 0;
unsigned long lastMqttReconnectAttempt = 0;
unsigned long lastStatusDisplayMillis = 0;

void showMessage(const char *message);

// Draw a temperature and keep two- and three-digit values visually centered.
void drawTemperature(int value, int xForTwoDigits, int xForThreeDigits, int y) {
  display.setCursor(value < 100 ? xForTwoDigits : xForThreeDigits, y);
  display.setTextSize(2);
  display.print(value);
  display.setTextSize(1);
  display.print((char)247);
  display.print("C");
}

// Draw one frame of the brewing animation. Frames count down so the loop can
// simply decrement coffeeCupFrame and reset it at the end.
void drawCoffeeCupFrame(int frame) {
  switch (frame) {
    case 8:
      display.drawBitmap(17, 14, coffeeCup30_01, 30, 30, WHITE);
      break;
    case 7:
      display.drawBitmap(17, 14, coffeeCup30_02, 30, 30, WHITE);
      break;
    case 6:
      display.drawBitmap(17, 14, coffeeCup30_03, 30, 30, WHITE);
      break;
    case 5:
      display.drawBitmap(17, 14, coffeeCup30_04, 30, 30, WHITE);
      break;
    case 4:
      display.drawBitmap(17, 14, coffeeCup30_05, 30, 30, WHITE);
      break;
    case 3:
      display.drawBitmap(17, 14, coffeeCup30_06, 30, 30, WHITE);
      break;
    case 2:
      display.drawBitmap(17, 14, coffeeCup30_07, 30, 30, WHITE);
      break;
    case 1:
      display.drawBitmap(17, 14, coffeeCup30_08, 30, 30, WHITE);
      break;
    default:
      display.drawBitmap(17, 14, coffeeCup30_00, 30, 30, WHITE);
      break;
  }
}

void wifiSetup() {

  gotIpEventHandler = WiFi.onStationModeGotIP([](const WiFiEventStationModeGotIP &event) {
    Serial.print("WiFi connected, IP: ");
    Serial.println(WiFi.localIP());
    Serial.print("Signal level: ");
    ltoa(WiFi.RSSI(), signalLevel, 10);
    Serial.println(signalLevel);
  });

  disconnectedEventHandler = WiFi.onStationModeDisconnected([](const WiFiEventStationModeDisconnected &event) {
    Serial.println("WiFi disconnected");
  });

  Serial.printf("Connecting to %s ...\n", wifiSSID);
  WiFi.mode(WIFI_STA);
  WiFi.hostname("MaraX");
  WiFi.begin(wifiSSID, wifiPassword);
}

void otaSetup() {
  ArduinoOTA.setHostname(otaHostname);
  ArduinoOTA.setPassword(otaPassword);

  ArduinoOTA.onStart([]() {
    Serial.println("OTA update started");
    showMessage("OTA...");
  });

  ArduinoOTA.onEnd([]() {
    Serial.println("OTA update finished");
  });

  ArduinoOTA.onError([](ota_error_t error) {
    Serial.print("OTA error ");
    Serial.println(error);
  });
}

void ensureOtaStarted() {
  if (otaStarted || WiFi.status() != WL_CONNECTED) {
    return;
  }

  ArduinoOTA.begin();
  otaStarted = true;
  Serial.println("OTA ready");
}

// Parse a complete line from the MaraX serial interface.
// Expected frames:
//   V1: C1.23,045,124,093,0840,1
//   V2: C1.23,045,124,093,0840,1,0
//   mode+firmware, steam, target steam, HX, boost countdown, heat, [pump]
//
// V2 sends seven fields including pump state. V1 has no serial pump state, so
// six fields are enough; the loop later overwrites pumpState from the reed
// contact. Strict field counts keep malformed serial data from shifting values
// into the wrong MQTT topic or display position.
bool parseMaraFrame(char *frame, MaraData &receivedData) {
  char *fields[7];
  int fieldCount = 0;

  char *ptr = strtok(frame, ",");
  while (ptr != NULL) {
    if (fieldCount >= 7) {
      return false;
    }
    fields[fieldCount++] = ptr;
    ptr = strtok(NULL, ",");
  }

#ifdef MARA_V1
  const int minFieldCount = 6;
#else
  const int minFieldCount = 7;
#endif

  if (fieldCount < minFieldCount || fieldCount > 7 || strlen(fields[0]) < 2) {
    return false;
  }

  receivedData.updated = true;
  receivedData.mode[0] = fields[0][0];
  receivedData.mode[1] = '\0';
  strncpy(receivedData.firmware, fields[0] + 1, sizeof(receivedData.firmware) - 1);
  receivedData.firmware[sizeof(receivedData.firmware) - 1] = '\0';
  receivedData.steamTemp = atoi(fields[1]);
  receivedData.targetSteamTemp = atoi(fields[2]);
  receivedData.hxTemp = atoi(fields[3]);
  receivedData.boostCountdown = atoi(fields[4]);
  receivedData.heatState = atoi(fields[5]);
  receivedData.pumpState = fieldCount == 7 ? atoi(fields[6]) : 0;

  return true;
}

MaraData getSimulatedMaraData() {
  MaraData receivedData;
  receivedData.updated = true;
  strcpy(receivedData.mode, "C");
  strcpy(receivedData.firmware, "1.23");
  receivedData.steamTemp = 116;
  receivedData.targetSteamTemp = 124;
  receivedData.hxTemp = 92;
  receivedData.boostCountdown = 840;
  receivedData.heatState = 1;
  receivedData.pumpState = 0;
  return receivedData;
}


// Read bytes from the MaraX serial interface and return a data object only
// when a full valid frame was received. If no bytes arrive for a while, mark
// the machine as off so the display can switch to the OFF screen.
MaraData getMaraData() {
  /*
    Example V1 Data: C1.23,045,124,093,0840,1\n
    Example V2 Data: C1.23,045,124,093,0840,1,0\n
    every ~400-500ms
    [Pos] [Data] [Describtion]
    0)      C     Coffee Mode (C) or SteamMode (V) // "+" in case of Mara X V2 Steam Mode
    -       1.23  Software Version
    1)      116   current steam temperature (Celsius)
    2)      124   target steam temperature (Celsius)
    3)      093   current hx temperature (Celsius)
    4)      0840  countdown for 'boost-mode'
    5)      1     heating element on or off
    6)      0     pump on or off // only for Mara X V2
  */
  MaraData receivedData;
  // Serial.print("Serial data available: ");
  // Serial.println(MaraRxSerial.available());

  while (MaraRxSerial.available() > 0) { // true as long there are chrs in the Rx buffer
    maraIsOff = false;
    lastSerialRxMillis = millis();
    char rcv = MaraRxSerial.read();        // read next chr
    if (rcv == '\r') {
      continue;
    }
    if (rcv != '\n') {                     // test if not LF
      if (serialFrameOverflow) {
        continue;
      } else if (bufferIndex < BUFFER_SIZE - 1) {
        buffer[bufferIndex++] = rcv;       // add to buffer and increase counter
      } else {
        bufferIndex = 0;
        serialFrameOverflow = true;
        Serial.println("Serial frame too long, discarding");
      }
    } else if (serialFrameOverflow) {
      serialFrameOverflow = false;
      bufferIndex = 0;
    } else if (bufferIndex > 0) {          // LF received = EOM
      buffer[bufferIndex] = '\0';
      bufferIndex = 0;
      if (parseMaraFrame(buffer, receivedData)) {
        return receivedData;
      }
      Serial.println("Invalid serial frame, discarding");
    }
  }

  if (millis() - lastSerialRxMillis > SERIAL_TIMEOUT_MS) {
    lastSerialRxMillis = millis();
    bufferIndex = 0;
    serialFrameOverflow = false;
    MaraRxSerial.write(0x11);
    if (SIMULATE_MARA_RX) {
      Serial.println("No Rx, using simulated Rx data");
      maraIsOff = false;
      return getSimulatedMaraData();
    }
    maraIsOff = true;
  }
  return receivedData;
}

void showMessage(const char *message) {
  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(WHITE);
  display.setCursor(0, 16);
  display.println(message);
  display.display();
}

void showSystemStatus(const char *dataStatus) {
  display.clearDisplay();
  display.setTextColor(WHITE);

  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("MaraX ShotTimer");

  display.setCursor(0, 18);
  display.print("WiFi ");
  display.print(WiFi.status() == WL_CONNECTED ? "OK" : "WAIT");

  display.setCursor(0, 32);
  display.print("MQTT ");
  display.print(mqttClient.connected() ? "OK" : "WAIT");

  display.setCursor(0, 46);
  display.print("Data ");
  display.print(dataStatus);

  display.display();
}

void updateSystemStatus(const char *dataStatus) {
  unsigned long now = millis();
  if (lastStatusDisplayMillis != 0 && now - lastStatusDisplayMillis < STATUS_DISPLAY_INTERVAL_MS) {
    return;
  }

  lastStatusDisplayMillis = now;
  showSystemStatus(dataStatus);
}

// Try one MQTT reconnect attempt every MQTT_RECONNECT_INTERVAL_MS. A blocking
// reconnect loop would freeze the display and serial parser when Home Assistant
// or WiFi is temporarily unavailable.
bool connectMQTT() {
  if (mqttClient.connected()) {
    return true;
  }

  if (WiFi.status() != WL_CONNECTED) {
    return false;
  }

  unsigned long now = millis();
  if (lastMqttReconnectAttempt != 0 && now - lastMqttReconnectAttempt < MQTT_RECONNECT_INTERVAL_MS) {
    return false;
  }
  lastMqttReconnectAttempt = now;

  Serial.print("Connecting to MQTT broker on ");
  Serial.print(mqttServer);
  Serial.print(" ... ");
  if (mqttClient.connect(MQTT_CLIENT_ID, MQTT_USER, MQTT_PASSWORD)) {
    Serial.println("connected");
    return true;
  }

  Serial.print("failed, state=");
  Serial.println(mqttClient.state());
  return false;
}

void publishMQTT(MaraData data) {
  if (!mqttClient.connected() && !connectMQTT()) {
    return;
  }

  unsigned long now = millis();
  if (now - lastMsg > mqttUpdateInterval) { // Check MQTT timer interval
    char steamTemp[12];
    char targetSteamTemp[12];
    char hxTemp[12];
    char boostCountdown[12];
    char heatState[12];
    char pumpState[12];
    snprintf(steamTemp, sizeof(steamTemp), "%d", data.steamTemp);
    snprintf(targetSteamTemp, sizeof(targetSteamTemp), "%d", data.targetSteamTemp);
    snprintf(hxTemp, sizeof(hxTemp), "%d", data.hxTemp);
    snprintf(boostCountdown, sizeof(boostCountdown), "%d", data.boostCountdown);
    snprintf(heatState, sizeof(heatState), "%d", data.heatState);
    snprintf(pumpState, sizeof(pumpState), "%d", data.pumpState);

    Serial.println("MQTT sending message now");
    bool published = true;
    published &= mqttClient.publish("SmartHome/MaraX/firmware", data.firmware);
    published &= mqttClient.publish("SmartHome/MaraX/mode", data.mode);
    published &= mqttClient.publish("SmartHome/MaraX/steamTemp", steamTemp);
    published &= mqttClient.publish("SmartHome/MaraX/targetSteamTemp", targetSteamTemp);
    published &= mqttClient.publish("SmartHome/MaraX/hxTemp", hxTemp);
    published &= mqttClient.publish("SmartHome/MaraX/heatState", heatState);
    published &= mqttClient.publish("SmartHome/MaraX/boostCountdown", boostCountdown);
    published &= mqttClient.publish("SmartHome/MaraX/pumpState", pumpState);
    published &= mqttClient.publish("SmartHome/MaraX/WiFiRxLevel", signalLevel);

    if (published) {
      lastMsg = now;
      Serial.println("MQTT Data published");
    } else {
      Serial.println("MQTT publish failed");
    }
  }
}

// Render the complete OLED view.
// - During a shot, show the shot timer and animated cup.
// - Otherwise, show HX/steam temperatures, heat state, WiFi and machine mode.
void updateView(int hxTemp, int steamTemp, int heatState, const char *mode) {

  display.clearDisplay();
  display.setTextColor(WHITE);

  if (shotSeconds > SHOT_TIMER_DISPLAY_AFTER_SECONDS) {
    // draw the timer on the right
    display.fillRect(60, 9, 63, 55, BLACK);
    display.setTextSize(5);
    display.setCursor(68, 20);
    char actual[3];
    snprintf(actual, sizeof(actual), "%02d", shotSeconds);
    display.print(actual); // Display seconds on screen

    if (coffeeCupFrame >= 1) {
      drawCoffeeCupFrame(coffeeCupFrame);
      if (coffeeCupFrame == 1) {
        coffeeCupFrame = COFFEE_CUP_FIRST_FRAME;
      } else {
        coffeeCupFrame--;
      }
    }

    drawTemperature(hxTemp, 19, 9, 50);
  } else { //Coffee temperature and bitmap
    display.drawBitmap(17, 16, coffeeCup30_00, 30, 30, WHITE);
    drawTemperature(hxTemp, 19, 9, 50);

    //Steam temperature and bitmap
    display.drawBitmap(83, 16, steam30, 30, 30, WHITE);
    drawTemperature(steamTemp, 88, 78, 50);

    // Draw line
    display.drawLine(66, 16, 66, 64, WHITE);

    // Boiler is heating up
    if (heatState == 1) {
      display.setCursor(13, 0);
      display.setTextSize(1);
      display.print("Heatup");

      if ((millis() - lastHeatBlinkMillis) > HEAT_BLINK_INTERVAL_MS) {
        lastHeatBlinkMillis = millis();
        heatBlinkOn = !heatBlinkOn;
      }
      if (heatBlinkOn) {
        display.fillRect(0, 0, 12, 12, BLACK);
        display.drawCircle(3, 3, 3, WHITE);
        display.fillCircle(3, 3, 2, WHITE);
      } else {
        display.fillRect(0, 0, 12, 12, BLACK);
        display.drawCircle(3, 3, 3, WHITE);
      }
    } else {
      display.print(""); // Clear heatup message
      display.fillCircle(3, 3, 3, BLACK);
    }

    // WiFi Signal
    if (wifi.connected()) {
      display.drawBitmap(60, 0, wifiicon, 12, 12, WHITE); // Draw WiFi icon in upper center
      display.setCursor(75, 2);
      display.setTextSize(1);
      //display.print(signalLevel);
      //display.print("dB");
    } else {
      display.fillRect(60, 0, 12, 12, BLACK); // Clear WiFi icon when not connected
      display.setCursor(75, 2);
      display.print("       ");
    }

    // Draw machine mode
    if (mode[0] == 'C') {                                  // "C" = coffe mode
      display.drawBitmap(115, 0, coffeeCup12, 12, 12, WHITE); // Draw coffee cup icon in upper right corner
    } else {                                                  // Steam mode
      display.drawBitmap(115, 0, steam12, 12, 12, WHITE);     // Draw steam icon in upper right corner
    }
  }

  display.display();
}

// Maintain the shot timer state from the effective pump state.
// V1 passes a debounced reed-contact value here, V2 passes the serial value.
void updateShotTimer(int pumpState) {
  unsigned long now = millis();

  if (pumpState == 1) {
    if (!shotTimerRunning) {
      shotTimerRunning = true;
      pumpOffSinceMillis = 0;
      shotTimerStoppedMillis = 0;
      lastShotSecondMillis = now;
      shotSeconds = 0;
      coffeeCupFrame = COFFEE_CUP_FIRST_FRAME;
      Serial.println("Pump on");
    }

    if (now - lastShotSecondMillis >= 1000) {
      lastShotSecondMillis = now;
      ++shotSeconds;
      if (shotSeconds > SHOT_TIMER_MAX_SECONDS) {
        shotSeconds = 0;
      }
    }
    return;
  }

  if (shotTimerRunning) {
    Serial.println("Pump off");
    shotTimerRunning = false;
    pumpOffSinceMillis = 0;
    shotTimerStoppedMillis = now;
  }

  if (shotTimerStoppedMillis != 0 && now - shotTimerStoppedMillis >= SHOT_TIMER_HOLD_AFTER_PUMP_OFF_MS) {
    shotTimerStoppedMillis = 0;
    shotSeconds = 0;
    coffeeCupFrame = COFFEE_CUP_FIRST_FRAME;
  }
}

void setup() {
  // Setup Serials
  Serial.begin(9600);
  MaraRxSerial.begin(9600);

  // Setup display
  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println(F("SSD1306 allocation failed"));
    for (;;)
      ; // Don't proceed, loop forever
  }
  display.display();
  delay(500);
  display.clearDisplay();
  showMessage("Starting...");

#ifdef MARA_V1
  pinMode(PUMP_PIN, INPUT_PULLUP);
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);
#endif

  showMessage("WiFi...");
  wifiSetup();
  otaSetup();

  // Set our MQTT broker address and port
  mqttClient.setServer(mqttServer, mqttPort);

  Serial.println("Setup done");
  showSystemStatus("WAIT");

  delay(100);
}

void loop() {
  ensureOtaStarted();
  if (otaStarted) {
    ArduinoOTA.handle();
  }

  if (mqttClient.connected()) {
    mqttClient.loop();
  } else {
    connectMQTT();
  }

#ifdef MARA_V1
  // Sample the reed contact every loop iteration. The reed signal can be
  // pulse-like, so reading it only when a serial frame arrives would miss
  // activity and make the shot timer stop while brewing.
  int effectivePumpState = stabilizeV1PumpState(readV1PumpState());
  updateShotTimer(effectivePumpState);
#endif

  // Get data
  MaraData data = getMaraData();
  if (data.updated == true) {
    hasReceivedMaraData = true;
#ifdef MARA_V1
    // V1 has no serial pump state, so use the loop-sampled reed contact.
    data.pumpState = effectivePumpState;
#else
    updateShotTimer(data.pumpState);
#endif
    updateView(data.hxTemp, data.steamTemp, data.heatState, data.mode);
    publishMQTT(data);

  } else if (maraIsOff) {
    updateSystemStatus("OFF");
  } else if (!hasReceivedMaraData) {
    updateSystemStatus("WAIT");
  }
}
