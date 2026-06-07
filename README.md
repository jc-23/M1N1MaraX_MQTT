This is an external ESP8266-based shot timer and monitor for Lelit MaraX V1/V2 espresso machines.

It runs on an ESP8266/Wemos D1 mini with a 128x64 SSD1306 OLED display.

The shot timer supports both MaraX V1 and V2:

- MaraX V2 reports the pump state through the serial data frame.
- MaraX V1 does not provide a serial pump state, so the firmware can read a reed contact and debounces short off glitches before stopping the shot timer.

The active build currently has `MARA_V1` enabled in `M1N1MaraX_MQTT.cpp`. For a V2 machine, disable that define so the serial pump state is used directly.

## What It Does

In idle mode the display shows:

1. Heater status in the upper left corner.
2. WiFi icon when connected.
3. MaraX operating mode in the upper right corner.
4. Heat exchanger and steam boiler temperatures.

During brewing, the steam temperature area is replaced by a shot timer. A filling coffee cup animation is shown together with the heat exchanger temperature.

The following values are published to MQTT:

1. MaraX firmware version reported by the serial interface.
2. Operating mode.
3. Steam temperature.
4. Target steam temperature.
5. Heat exchanger temperature.
6. Boost countdown.
7. Heating element state.
8. Pump state.
9. WiFi signal level.

## Configuration

Local configuration is done with `secrets.h`.
To create it, copy `secrets.example.h` to `secrets.h` and adjust the values:

```cpp
#define WLAN_SSID "your-wifi-ssid"
#define WLAN_PASS "your-wifi-password"
#define MQTT_SERVER "192.168.1.100"
#define MQTT_PORT 1883
#define MQTT_UPDATE_INTERVAL 30
#define MQTT_USER "marax"
#define MQTT_PASSWORD "your-mqtt-password"
```

The machine appears in the router as `MaraX`.

## V1 And V2 Notes

For MaraX V1, keep `MARA_V1` enabled. The shot timer reads the reed contact on `PUMP_PIN` and stabilizes the pump state with `V1_PUMP_OFF_DEBOUNCE_MS`. If the timer still disappears during a shot, increase that value. If the timer stays visible too long after brewing stops, reduce it.

For MaraX V2, disable `MARA_V1`. The firmware will then use the pump state that comes from the serial interface.

The Arduino TX line / Gicar RX line does not need to be connected. The shot timer only needs to receive data from the machine.

## Hardware

- ESP8266 / Wemos D1 mini or compatible board.
- 128x64 SSD1306 OLED display.
- MaraX serial connection.
- For MaraX V1: reed contact on `D7`/GPIO13 for pump detection.

Please take care when wiring the MaraX Gicar control box. There is conflicting information online about the interface, especially for V2 machines. Please see for proper wiring and instructions:
https://www.m1n1.de/en/lelit-mara-x-v2-gicar-internals/

![image](https://github.com/dougie996/M1N1MaraX_MQTT/assets/117717919/8c066df9-6e21-4d42-b458-7699bd4b0714)
