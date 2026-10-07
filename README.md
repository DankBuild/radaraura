# RadarAura

A desk air-quality monitor: a 4.3" ESP32-P4 touch display plus a Sensirion SEN66
sensor in a 3D-printed case. It shows CO2, particulate matter (PM1/2.5/4/10), VOC
and NOx indices, temperature and humidity, with a one-hour CO2 trend and a plain
"what should I do" line.

**Build guide with wiring pictures: <https://radaraura.com/build/>**

<p>
  <img src="docs/device_front.jpg" width="300" alt="The finished RadarAura monitor in its white printed case, the screen showing an air score of 30 and CO2 at 1405 ppm">
  <img src="docs/device_back.jpg" width="300" alt="The back of the case with the radar mark, the RadarAura name and the USB-C cable">
</p>

<p>
  <img src="docs/03_good.png" width="190" alt="Dashboard with fresh air">
  <img src="docs/04_stale.png" width="190" alt="Dashboard with high CO2">
  <img src="docs/05_smoke.png" width="190" alt="Smoke warning">
  <img src="docs/06_info_co2.png" width="190" alt="CO2 explanation sheet">
</p>

**Private by design:** no WiFi, no Bluetooth, no microphone. The board's ESP32-C6
radio chip is held in reset from the first instruction and no radio stack is
compiled in; the microphone is never switched on. Everything stays on the device.

## What you need

| # | Get this | Notes |
|---|---|---|
| 1 | **Guition JC4880P443C-I-W**, the bare board | ESP32-P4 with a 4.3" 480x800 touch screen (ST7701). Buy the version **without** the factory case (the one in its own plastic shell is usually listed with a "-Y" at the end); the RadarAura case is made for the bare board |
| 2 | **Sensirion SEN66** | PM, RH/T, VOC, NOx and CO2 in one module. Not the SEN63C, SEN65 or SEN68 |
| 3 | **SEN66 cable**: 6-pin, 1.25 mm pitch, JST GH type | Often sold separately, so check your order includes it. Wire colours don't matter |
| 4 | **A 3D printer, or someone to print for you** | Three parts in PETG, see [`case/`](case/README.md): one-colour or two-colour. A friend, makerspace, library or print service can print the STLs |

Tools: a soldering iron, heat-shrink, a USB-C cable with a USB power supply, and a
computer with ESP-IDF 5.3 to flash the firmware once. Optional: 4x M2 pan-head
screws (the case plugs tightly together and holds without them) and a second
filament colour for the name and radar mark. No resistors or other extra parts
are needed.

**Skill:** basic soldering. The four wires are soldered to the header pins. Female
jumper wires pushed onto the pins can work for a quick test, but they can slip or
lose contact when the case is closed ("Sensor offline"), and one slipping onto the
5 V pin next to pin 1 can damage the SEN66. Solder them for a device you keep.

**Wiring:** only 4 of the SEN66's 6 wires are needed (pins 5 and 6 repeat GND
and 3.3 V inside the sensor). They go to the board's 2x13 header, on the row
nearest the USB-C ports:

| SEN66 wire | Signal | Header pin |
|---|---|---|
| 1 (end of the plug farthest from the round outlet) | 3.3 V | 1 |
| 2 | GND | 5 |
| 3 | SDA (GPIO7) | 23 |
| 4 | SCL (GPIO8) | 25 |

Ignore the wire colours: these clip-in cables are not colour-coded consistently,
so red is not always 3.3 V and black is not always GND. Count by position from
wire 1.

Slide heat-shrink onto each wire before soldering, then shrink it over the joint:
no bare metal may touch a neighbouring pin (23 and 25 sit side by side, and the
5 V pins are next to pin 1).

The first two pins of the other row carry **5 V**: don't use them. I2C address
0x6B; the board already has the pull-ups. Pictures: <https://radaraura.com/build/#wiring>.

## Firmware

Requires [ESP-IDF](https://docs.espressif.com/projects/esp-idf/) 5.3 (tested with 5.3.5).

```sh
cd firmware
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

RadarAura has no speaker: warnings, including the smoke warning, are shown on
the screen. The firmware still contains alert-sound code (settings at the top of
`firmware/main/main.c`, spoken alerts via `tools/generate_tts.py`), but the
standard build has nothing to play them on.

> The smoke warning is a convenience, **not a certified smoke detector**. Use a
> real smoke alarm for life safety.

### UI screenshots without hardware

`tools/ui_sim/build.sh` renders the dashboard on a PC to PNGs (run a firmware
build first; it reuses LVGL from `firmware/managed_components`).

## Case

See [`case/README.md`](case/README.md).

## Licence

Free to build and change for yourself, with the RadarAura name kept on the case
and the screen; selling requires permission. Everything is under the
[RadarAura Build Licence](LICENSE.md); the name rules are summed up in
[TRADEMARK.md](TRADEMARK.md).

Copyright (c) 2026 DankBuild.
