# Hyperwisor HMI

Build a connected touch panel. This repository gives you a board and a
connection; **the screens are yours.**

There is no UI framework here above LVGL, and no schema to conform to. The
examples are starting points to copy and change, not a runtime to configure.

```
components/
  hmi_bsp_waveshare7/   display, touch, backlight, LVGL port
  esp_lcd_touch/        touch driver core
  esp_lcd_touch_gt911/  GT911 controller
examples/
  01_hello_panel/       bring-up, identity, connection state
partitions/
  partitions_ota.csv    16 MB: two 3.5 MB app slots + 2 MB filesystem
```

The IoT half — Wi-Fi, provisioning, device identity, the relay, OTA — is the
separate [`hyperwisor`](https://github.com/nikolaindustry/hyperwisor_esp_idf)
component. It is headless and runs on any ESP32, so a sensor node does not pull
in a display stack.

## Quick start

You need ESP-IDF 5.5 and a checkout of `hyperwisor_esp_idf` **beside** this
repository:

```
~/src/hyperwisor_esp_idf
~/src/hyperwisor-hmi        <- you are here
```

```bash
. ~/.espressif/v5.5/esp-idf/export.sh
cd examples/01_hello_panel
idf.py set-target esp32s3
idf.py -p /dev/cu.usbmodemXXXX flash monitor
```

The panel shows its device id and connection state. The log prints a public key
once — register it with the platform and the panel appears in your account.

## Hardware

Developed on the **Waveshare ESP32-S3-Touch-LCD-7** (800×480 RGB, GT911 touch,
CH422G expander, 16 MB flash, 8 MB octal PSRAM). A different panel means a
different `hmi_bsp_*` component; nothing above it should need to change.

## Things that will bite you

Each of these cost real time, so they are written down rather than rediscovered.

- **Initialise NVS before starting the RGB panel.** The RGB DMA end-of-frame ISR
  reads the PSRAM bounce buffer, and a flash read disables the flash/PSRAM cache
  while it runs. An ISR firing mid-read panics with *"Cache disabled but cached
  memory region accessed"*.
- **Take the LVGL lock for every UI touch from another task.** LVGL is not
  thread-safe, and a panel repainted from two tasks fails in ways that look like
  a driver fault.
- **The default 1 MB app partition is too small.** LVGL plus TLS lands around
  1.3 MB. Use `partitions/partitions_ota.csv`.
- **LVGL's `CONFIG_LV_ASSERT_HANDLER_INCLUDE` defaults to an empty string**,
  which it emits as `#include ""` and fails to compile. Set it to `assert.h`.
- **A `path:` dependency is named after its directory**, not the key in your
  manifest — so the path must end in a directory called exactly `hyperwisor`.
- **Wi-Fi power save is an explicit choice.** A mains-powered panel wants
  `HYPERWISOR_WIFI_PS_NONE`. The IDF default (`MIN_MODEM`) misses DTIM beacons
  on a busy band and disassociates, which presents as a server problem.

## Status

Early. `01_hello_panel` builds and runs; more examples are coming — both
horizontal (patterns every panel needs: a control tile, a sensor readout, OTA
with progress, offline behaviour) and vertical (complete products: vehicle,
industrial, home automation).

**Known limitation:** the board port targets **LVGL 8.4**. LVGL 9 replaced the
display and input driver API wholesale (`lv_disp_drv_t` and friends), so
`lvgl_port.c` needs porting before the examples can move to 9.

## Licence

Apache-2.0, same as the `hyperwisor` component.
