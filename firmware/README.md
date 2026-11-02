# fw-probe — the starter firmware the smoke tests boot

`fw-probe/src/main.cpp` (MIT) is a blink + button + UART program, one source
for every chip here:

- the UART prints `boot <mcu>` once at 115200 baud;
- the LED toggles every 250 ms (`millis()`), and each toggle prints
  `led=<0|1> btn=<0|1> t=<ms>`;
- the button (to ground, `INPUT_PULLUP`, debounced 20 ms) prints `pressed` /
  `released`.

| image | file | board | LED | button | UART |
|---|---|---|---|---|---|
| esp32c3-qemu | `esp32c3-fw-probe.flash.bin.gz` | `esp32-c3-devkitm-1`, 4 MB DIO | GPIO8 | GPIO9 | UART0 (TX GPIO21) |
| esp32-qemu | `esp32-fw-probe.flash.bin.gz` | `esp32dev`, 4 MB DIO | GPIO2 | GPIO0 | UART0 (TX GPIO1) |
| esp8266-emu | `esp8266-fw-probe.flash.bin.gz` | `d1_mini` (ESP-12), 4 MB DIO | GPIO2 | GPIO0 | UART0 (TX GPIO1) |

Each `.flash.bin.gz` is the full flash image the simulator boots (bootloader,
partition table, `boot_app0.bin`, the app, 0xFF between, padded to 4 MB), as
`esptool merge-bin --pad-to-size 4MB` lays it out, gzipped with `-9 -n` — for the ESP32s.
The ESP8266's is Arduino-ESP8266's own `firmware.bin` (eboot, then the app, from
0x0), which the emulator boots as it is; the flash size comes from its header.
`SHA256SUMS` holds the sha256 of each unzipped image.

## Rebuild

    firmware/build.sh

builds them all in a container (`firmware/Dockerfile`: Ubuntu 24.04 by digest,
PlatformIO 6.1.18, `platformio/espressif32@8.0.0`, Arduino-ESP32 3.3.x;
`platformio/espressif8266@4.2.1`, Arduino-ESP8266 3.1.2), at
neutral paths, and rewrites the `.gz` files and `SHA256SUMS`. The package
versions PlatformIO resolved are in `PACKAGES.txt`.

## Licences of what the prebuilt images contain

The images are compiled from `fw-probe` (MIT) and link
[Arduino-ESP32](https://github.com/espressif/arduino-esp32) (LGPL-2.1-or-later)
and the [ESP-IDF](https://github.com/espressif/esp-idf) libraries it ships
(Apache-2.0, plus the licences of their bundled third-party components, such
as newlib's). The ESP8266's links
[Arduino-ESP8266](https://github.com/esp8266/Arduino) (LGPL-2.1-or-later) and the
ESP8266 NONOS SDK libraries it ships (Espressif's, under their own licence
terms in that repository). Their sources are public at the versions in `PACKAGES.txt`;
the build above reproduces the images from them.
