# MaraX Shot Timer

External ESP8266 shot timer and machine monitor for the Lelit MaraX. The
repository provides two firmware variants for the same Wemos D1 mini and
SSD1306 display:

- **ESPHome** — recommended when the machine is connected to Home Assistant.
  It uses the native ESPHome API and creates all entities automatically.
- **PlatformIO** — standalone Arduino firmware with MQTT and Home Assistant
  MQTT Discovery.

Both variants display the heat-exchanger and steam temperatures, heating
state, operating mode, shot time, and the animated coffee cup. The active
hardware configuration targets a MaraX V1 and detects the pump through a reed
contact.

## Hardware setup

### Parts

- Wemos D1 mini or compatible ESP8266 board
- SSD1306 128x64 I²C OLED, normally at address `0x3C`
- Connection to the MaraX/Gicar serial transmit signal
- MaraX V1 only: reed contact for pump detection
- Suitable wiring and, where required by the controller revision, level
  adaptation for the serial signal

### Connections

| Function | Wemos pin | ESP8266 GPIO | Connect to |
| --- | --- | --- | --- |
| OLED SCL | D1 | GPIO5 | SSD1306 SCL |
| OLED SDA | D2 | GPIO4 | SSD1306 SDA |
| MaraX serial RX | D5 | GPIO14 | MaraX/Gicar serial TX |
| Pump detection (V1) | D7 | GPIO13 | Reed contact to GND |

Connect the OLED to 3.3 V and GND. The reed input uses the ESP8266's internal
pull-up resistor and is active when the contact connects D7 to GND. If the
contact behaves in the opposite direction, its polarity must be adjusted in
the selected firmware.

The MaraX serial interface sends at 9600 baud with inverted logic. The shot
timer only receives data from the machine:

- connect MaraX/Gicar TX to D5, the ESP receive pin;
- do not connect the ESP TX line to the Gicar RX line;
- ensure the ESP and serial interface have a common reference/GND;
- verify the electrical signal level for the exact Gicar revision before
  connecting it to the ESP8266.

There is conflicting wiring information for different MaraX controller
revisions, particularly the V2. The
[M1N1 MaraX V2 Gicar internals documentation](https://www.m1n1.de/en/lelit-mara-x-v2-gicar-internals/)
is a useful starting point, but the controller in the individual machine
should always be verified.

![MaraX Gicar connection example](https://github.com/dougie996/M1N1MaraX_MQTT/assets/117717919/8c066df9-6e21-4d42-b458-7699bd4b0714)

### MaraX V1 and V2

MaraX V1 serial frames do not contain the pump state. The timer therefore
uses the reed contact on D7 and filters short contact interruptions caused by
machine vibration.

MaraX V2 serial frames contain the pump state, so a reed contact is not
required when that field is used. The included ESPHome configuration currently
targets V1. The PlatformIO firmware supports both versions; its V1/V2 software
selection is described in the
[PlatformIO firmware README](src/README.md#marax-v1-and-v2-configuration).

> **Warning**
>
> Work inside an espresso machine involves mains voltage, heat, and pressure.
> Disconnect the machine from power before opening it. Verify the voltage and
> signal levels of your particular controller revision before connecting the
> ESP8266.

## ESPHome installation

Copy the complete [`esphome`](esphome) directory into the configuration
directory used by the Home Assistant ESPHome add-on. Add these entries to the
add-on's `secrets.yaml`:

```yaml
wifi_ssid: "your-wifi-ssid"
wifi_password: "your-wifi-password"
ap_password: "choose-an-access-point-password"
```

Open `marax.yaml` in the ESPHome dashboard, validate it, and install it. The
first ESPHome installation normally requires USB; subsequent updates work
over the air. See the [ESPHome-specific instructions](esphome/README.md) for
details.

## PlatformIO installation

Copy `src/secrets.example.h` to `src/secrets.h` and enter the local Wi-Fi,
MQTT, and OTA settings. Build and upload the default environment:

```sh
pio run
pio run --target upload
```

After the first USB installation, the `d1_mini_ota` environment can be used
for OTA uploads. Supply its password without storing it in the repository:

```sh
PLATFORMIO_UPLOAD_FLAGS="--auth=your-ota-password" \
  pio run -e d1_mini_ota --target upload
```

More details and the published MQTT entities are documented in the
[PlatformIO firmware README](src/README.md).

## Repository layout

```text
.
├── esphome/
│   ├── components/marax/   # local ESPHome component and bitmap assets
│   └── marax.yaml
├── include/                # PlatformIO headers
├── lib/                    # PlatformIO libraries
├── src/                    # PlatformIO firmware
└── platformio.ini
```

The canonical bitmap data lives inside the ESPHome component. The small
`src/bitmaps.h` forwarding header lets the PlatformIO firmware reuse the same
asset without maintaining a duplicate.

## Credits and license

This project is based on
[M1N1MaraX_MQTT](https://github.com/dougie996/M1N1MaraX_MQTT), which is partly
based on
[MaraX-Shot-Monitor](https://github.com/Anlieger/MaraX-Shot-Monitor).
See [NOTICE](NOTICE) for the detailed provenance.

The project is licensed under
[GNU General Public License v3.0 or later](LICENSE).
