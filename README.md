This is an external ESP8266-based shot timer and monitor for Lelit MaraX V1/V2 espresso machines.

It runs on an ESP8266/Wemos D1 mini with a 128x64 SSD1306 OLED display.

The shot timer supports both MaraX V1 and V2:

- MaraX V2 reports the pump state through the serial data frame.
- MaraX V1 does not provide a serial pump state, so the firmware can read a reed contact and debounces short off glitches before stopping the shot timer.

The active build currently has `MARA_V1` enabled in `M1N1MaraX_MQTT.cpp`. For a V2 machine, disable that define so the serial pump state is used directly.

## What It Does

During startup or while waiting for valid MaraX data, the display shows a compact status screen:

1. `WiFi OK` / `WiFi WAIT`
2. `MQTT OK` / `MQTT WAIT`
3. `Data WAIT` while no valid serial frame has been received.
4. `Data OFF` when no serial data is received for the configured timeout.

In idle mode the display shows:

1. Shot timer uptime in the upper left corner.
2. Heater status in the top bar while heating.
3. MaraX operating mode in the upper right corner.
4. Heat exchanger and steam boiler temperatures in two centered fields.

During brewing, the steam temperature area is replaced by a shot timer and the vertical field separator is hidden. A filling coffee cup animation is shown together with the heat exchanger temperature.
After the pump stops, the last shot time remains visible for `SHOT_TIMER_HOLD_AFTER_PUMP_OFF_MS` before the display returns to idle mode.

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
10. Cumulative number of shots lasting at least `SHOT_COUNT_MIN_SECONDS`.

## Home Assistant MQTT Discovery

The firmware publishes retained MQTT Discovery configurations under the default
`homeassistant` discovery prefix. Home Assistant groups all entities under one
`MaraX Shot Timer` device:

1. Machine firmware.
2. Operating mode.
3. Steam temperature.
4. Target steam temperature.
5. Heat exchanger temperature.
6. Boost countdown.
7. Heating element state.
8. Pump state.
9. WiFi signal strength.
10. Shot count.

The device also publishes an availability state, so Home Assistant marks the
entities unavailable when the ESP8266 disconnects unexpectedly.

A pump cycle is counted once, when it ends, if it lasted at least
`SHOT_COUNT_MIN_SECONDS` (20 seconds by default). The retained shot count is
published as a `total_increasing` sensor, allowing Home Assistant to calculate
daily, weekly, monthly, or other period totals. Cleaning reminders and reset
logic should be implemented in Home Assistant.

The counter starts at zero after an ESP8266 restart. Home Assistant treats this
as a reset of the cumulative sensor and preserves its long-term statistics.

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
#define OTA_HOSTNAME "MaraX"
#define OTA_PASSWORD "your-ota-password"
```

The machine appears in the router as `MaraX`.

## OTA Updates

The first upload has to be done via USB. After that, the firmware can be updated over WiFi with the `d1_mini_ota` PlatformIO environment.
During an OTA upload, the OLED shows the current percentage and a progress bar.

Set `OTA_HOSTNAME` and `OTA_PASSWORD` in `secrets.h`. Use the same password in the `upload_flags` of the `d1_mini_ota` environment in `platformio.ini`.

Example:

```sh
pio run -e d1_mini_ota -t upload
```

## V1 And V2 Notes

For MaraX V1, keep `MARA_V1` enabled. V1 serial frames do not contain a pump state, so the firmware accepts six-field serial frames and reads the pump state from the reed contact on `PUMP_PIN`. The reed value is stabilized with `V1_PUMP_OFF_DEBOUNCE_MS`. If the timer still disappears during a shot, increase that value. If the timer stays visible too long after brewing stops, reduce it.

For MaraX V2, disable `MARA_V1`. V2 serial frames contain seven fields including the pump state, so the firmware will use the pump state from the serial interface.

The Arduino TX line / Gicar RX line does not need to be connected. The shot timer only needs to receive data from the machine.

## Hardware

- ESP8266 / Wemos D1 mini or compatible board.
- 128x64 SSD1306 OLED display.
- MaraX serial connection.
- For MaraX V1: reed contact on `D7`/GPIO13 for pump detection.

Please take care when wiring the MaraX Gicar control box. There is conflicting information online about the interface, especially for V2 machines. Please see for proper wiring and instructions:
https://www.m1n1.de/en/lelit-mara-x-v2-gicar-internals/

![image](https://github.com/dougie996/M1N1MaraX_MQTT/assets/117717919/8c066df9-6e21-4d42-b458-7699bd4b0714)
