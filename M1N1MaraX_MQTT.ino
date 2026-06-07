
/* Copyright (C) 2024 Ralf Grafe
This file is partly based on MaraX-Shot-Monitor <https://github.com/Anlieger/MaraX-Shot-Monitor>.

M1N1MaraX_MQTT is a free software you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

M1N1MaraX_MQTT is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

//Includes
#include "bitmaps.h"
#include "secrets.h"
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Arduino.h>
#include <ArduinoHttpClient.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <SoftwareSerial.h>
#include <Timer.h>
#include <Wire.h>
#include <avr/pgmspace.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MARA_V1

#define SCREEN_WIDTH 128 // width in px
#define SCREEN_HEIGHT 64 // height in px
#define OLED_RESET -1
#define SCREEN_ADDRESS 0x3C // or 0x3D - check datasheet or OLED display
#define BUFFER_SIZE 32

#define D5 (14) // D5 is Rx pin
#define D6 (12) // D6 is Tx pin
#ifdef MARA_V1
#define D7 (13) // D7 is pump pin
#define PUMP_PIN D7
const unsigned long V1_PUMP_OFF_DEBOUNCE_MS = 700;
bool reedOpenSensor = false;
#endif

#define INVERSE_LOGIC 1 // Use inverse logic for MaraX

#define DEBUG false
#define SIMULATE_MARA_RX false

const unsigned long SERIAL_TIMEOUT_MS = 1000;
const unsigned long MQTT_RECONNECT_INTERVAL_MS = 5000;
const char MQTT_CLIENT_ID[] = "MaraX";

// Secrets from secrets.h
const char *wifiSSID = WLAN_SSID;
const char *wifiPassword = WLAN_PASS;
const char *mqttServer = MQTT_SERVER;
const int mqttPort = MQTT_PORT;
const unsigned long mqttUpdateInterval = MQTT_UPDATE_INTERVAL * 1000UL; // MQTT update interval from secrets.h
const int targetHxTemp = 90;

// Instances
WiFiClient wifi;
WiFiEventHandler gotIpEventHandler, disconnectedEventHandler;
PubSubClient mqttClient(wifi);
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
SoftwareSerial MaraRxSerial(D5, D6, INVERSE_LOGIC); // Rx, Tx, Inverse_Logic
//Timer t;

// internals
char signalLevel[16] = "0";

unsigned long lastMillis = 0;
int seconds = 0;
int lastSeconds = 0;

unsigned long timerStartMillis = 0;
unsigned long timerStopMillis = 0;
unsigned long timerPumpDelay = 0;
int timerCount = 0;
bool timerStarted = false;

unsigned long serialTimeout = 0;
char buffer[BUFFER_SIZE];
size_t bufferIndex = 0;
bool serialFrameOverflow = false;
int isMaraOff = 0;
unsigned long lastToggleTime = 0;
int HeatDisplayToggle = 0;
int tt = 8;

bool initialReadyMessageSent = false;

#ifdef MARA_V1
int readV1PumpState() {
  int reedValue = digitalRead(PUMP_PIN);
  digitalWrite(LED_BUILTIN, reedValue);
  return reedOpenSensor ? reedValue : !reedValue;
}

int stabilizeV1PumpState(int rawPumpState) {
  if (rawPumpState == 1) {
    timerStopMillis = 0;
    return 1;
  }

  if (timerStarted) {
    if (timerStopMillis == 0) {
      timerStopMillis = millis();
    }
    if (millis() - timerStopMillis < V1_PUMP_OFF_DEBOUNCE_MS) {
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

  if (fieldCount != 7 || strlen(fields[0]) < 2) {
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
  receivedData.pumpState = atoi(fields[6]);

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


MaraData getMaraData() {
  /*
    Example Data: C1.23,045,124,093,0840,1,0\n every ~400-500ms
    Length: 27
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
    isMaraOff = 0;                         // Mara is on
    serialTimeout = millis();              // save current time
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

  if (millis() - serialTimeout > SERIAL_TIMEOUT_MS) { // have 1000ms passed after last chr received?
    serialTimeout = millis();
    bufferIndex = 0;
    serialFrameOverflow = false;
    MaraRxSerial.write(0x11);
    if (SIMULATE_MARA_RX) {
      Serial.println("No Rx, using simulated Rx data");
      isMaraOff = 0;
      return getSimulatedMaraData();
    }
    isMaraOff = 1;
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

void showMessageMaraOff() {
  display.clearDisplay();
  display.setCursor(35, 2);
  display.setTextSize(2);
  display.print("MaraX");
  display.setCursor(30, 28);
  display.setTextSize(4);
  display.print("OFF");
  display.display();
}

// Connect to MQTT broker without blocking the main loop indefinitely.
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

// void detectChanges(MaraData data) {
//   if (data.pumpState == 1) { // [6] == 1 is flag for pump ON
//     if (!timerStarted) {           // Timer is not started
//       timerStartMillis = millis(); // Save current time
//       timerStarted = true;
//       Serial.println("Pump ON");
//     }
//   }

//   if (data.pumpState == 0) { // [6] == 0 is flag for pump OFF
//     if (timerStarted) { // Check if Timer started Flag is set
//       if (timerStopMillis == 0) {
//         timerStopMillis = millis(); // Save current time
//       }
//       if (millis() - timerStopMillis > 500) { // this will be executed 500ms after Pump has been switched off
//         timerStarted = false;
//         timerStopMillis = 0;
//         //display.invertDisplay(false);
//         Serial.println("Pump OFF");
//         tt = 8;

//         timerPumpDelay = millis(); // Save current time
//         while (millis() - timerPumpDelay < 1000) { // wait 1 second
//           delay(200);
//         }
//       }
//     }
//   }
//   else {
//     timerStopMillis = 0;
//   }
// }

// String getTimer() {
//   char outMin[2];
//   if (timerStarted) {
//     timerCount = (millis() - timerStartMillis) / 1000;
//     if (timerCount > 4) {
//       prevTimerCount = timerCount;
//     }
//   } else {
//     timerCount = prevTimerCount;
//   }
//   if (timerCount > 99) {
//     return "99";
//   }
//   sprintf(outMin, "%02u", timerCount);
//   return outMin;
// }

void updateView(int hxTemp, int steamTemp, int pumpState, int heatState, const char *mode) {

  display.clearDisplay();
  display.setTextColor(WHITE);

  if (seconds > 3) {
    // draw the timer on the right
    display.fillRect(60, 9, 63, 55, BLACK);
    display.setTextSize(5);
    display.setCursor(68, 20);
    char actual[3];
    snprintf(actual, sizeof(actual), "%02d", seconds);
    display.print(actual); // Display seconds on screen

    if (tt >= 1) {
      // if (tt >= 1 && timerCount <= 23) {
      if (tt == 8) {
        display.drawBitmap(17, 14, coffeeCup30_01, 30, 30, WHITE);
        // Serial.println(tt);
      } else if (tt == 7) {
        display.drawBitmap(17, 14, coffeeCup30_02, 30, 30, WHITE);
        // Serial.println(tt);
      } else if (tt == 6) {
        display.drawBitmap(17, 14, coffeeCup30_03, 30, 30, WHITE);
        // Serial.println(tt);
      } else if (tt == 5) {
        display.drawBitmap(17, 14, coffeeCup30_04, 30, 30, WHITE);
        // Serial.println(tt);
      } else if (tt == 4) {
        display.drawBitmap(17, 14, coffeeCup30_05, 30, 30, WHITE);
        // Serial.println(tt);
      } else if (tt == 3) {
        display.drawBitmap(17, 14, coffeeCup30_06, 30, 30, WHITE);
        // Serial.println(tt);
      } else if (tt == 2) {
        display.drawBitmap(17, 14, coffeeCup30_07, 30, 30, WHITE);
        // Serial.println(tt);
      } else if (tt == 1) {
        display.drawBitmap(17, 14, coffeeCup30_08, 30, 30, WHITE);
        // Serial.println(tt);
      }
      if (tt == 1) {
        tt = 8;
      } else {
        tt--;
      }
    }

    if (hxTemp < 100) { // Taking care of 2 or 3 digit value display
      display.setCursor(19, 50);
    } else {
      display.setCursor(9, 50);
    }

    display.setTextSize(2);
    display.print(hxTemp);
    display.setTextSize(1);
    display.print((char)247);
    display.setTextSize(1);
    display.print("C");
  } else { //Coffee temperature and bitmap
    display.drawBitmap(17, 16, coffeeCup30_00, 30, 30, WHITE);
    if (hxTemp < 100) {
      display.setCursor(19, 50);
    } else {
      display.setCursor(9, 50);
    }
    display.setTextSize(2);
    display.print(hxTemp);
    display.setTextSize(1);
    display.print((char)247);
    display.setTextSize(1);
    display.print("C");

    //Steam temperature and bitmap
    display.drawBitmap(83, 16, steam30, 30, 30, WHITE);
    if (steamTemp < 100) {
      display.setCursor(88, 50);
    } else {
      display.setCursor(78, 50);
    }
    display.setTextSize(2);
    display.print(steamTemp);
    display.setTextSize(1);
    display.print((char)247);
    display.setTextSize(1);
    display.print("C");

    // Draw line
    display.drawLine(66, 16, 66, 64, WHITE);

    // Boiler is heating up
    if (heatState == 1) {
      display.setCursor(13, 0);
      display.setTextSize(1);
      display.print("Heatup");

      if ((millis() - lastToggleTime) > 1000) {
        lastToggleTime = millis();
        if (HeatDisplayToggle == 1) {
          HeatDisplayToggle = 0;
        } else {
          HeatDisplayToggle = 1;
        }
      }
      if (HeatDisplayToggle == 1) {
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

  // Set our MQTT broker address and port
  mqttClient.setServer(mqttServer, mqttPort);

  Serial.println("Setup done");
  showMessage("Ready!");

  delay(100);

  // t.every(1000, updateView);
}

void loop() {
  if (mqttClient.connected()) {
    mqttClient.loop();
  }

  // Get data
  MaraData data = getMaraData();
  if (data.updated == true) {
    // Check if machine has reached target HX temp as defined in settings
    if (data.steamTemp == data.targetSteamTemp && data.hxTemp > targetHxTemp && initialReadyMessageSent == false) {
      // sendPushSaferMessage();
      initialReadyMessageSent = true;
    }
#ifdef MARA_V1
    int rawPumpState = readV1PumpState();
    data.pumpState = stabilizeV1PumpState(rawPumpState);
    Serial.print("Mara V1 reed pump raw/effective: ");
    Serial.print(rawPumpState);
    Serial.print("/");
    Serial.println(data.pumpState);
#endif
    // Start timer
    if (data.pumpState == 1) {
      if (!timerStarted) {
        timerStarted = true;
        timerStopMillis = 0;
        lastMillis = millis();
        seconds = 0;
        tt = 8;
        Serial.println("Pump on");
      }
      if (millis() - lastMillis >= 1000) {
        lastMillis = millis();
        ++seconds;
        if (seconds > 99)
          seconds = 0;
      }
    } else {
      if (timerStarted) {
        Serial.println("Pump off");
        timerStarted = false;
        timerStopMillis = 0;
      }
      if (seconds != 0) {
        lastSeconds = seconds;
      }
      seconds = 0;
    }

    updateView(data.hxTemp, data.steamTemp, data.pumpState, data.heatState, data.mode);
    publishMQTT(data);

  } else if (isMaraOff == 1) {
    Serial.println("Mara is off");
    showMessageMaraOff();
    delay(1000);
  }

  //t.update();
  //detectChanges();
  //getMaraData();
}
