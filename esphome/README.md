# MaraX ESPHome

This directory contains the ESPHome replacement for the PlatformIO firmware.
It targets a MaraX V1, Wemos D1 mini, SSD1306 128x64 OLED and the reed contact
on D7. See the repository's
[main hardware setup](../README.md#hardware-setup) for the parts, wiring,
electrical notes, and V1/V2 differences.

## Install with the Home Assistant add-on

1. Copy `marax.yaml` and the complete `components` directory to
   `/config/esphome`. Keep this relative layout:

   ```text
   /config/esphome/
   ├── marax.yaml
   └── components/marax/
       ├── __init__.py
       ├── marax.h
       └── bitmaps.h
   ```

2. Ensure `secrets.yaml` contains `wifi_ssid`, `wifi_password`, and
   `ap_password`; `secrets.example.yaml` shows the expected names.
3. Validate and install `marax.yaml` from the ESPHome dashboard. The first
   ESPHome installation should be done over USB. Later updates work OTA.
4. Add the discovered `MaraX` device to Home Assistant.

The native ESPHome API replaces MQTT. The shot counter deliberately starts at
zero after a device restart, matching the old firmware; Home Assistant handles
this as a reset of a `total_increasing` sensor.
