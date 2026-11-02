# esp32-qemu — the ESP32 (classic) simulator

`ghcr.io/heypcb/esp32-qemu` runs ESP32 (classic, Xtensa LX6: the ESP32-D0WD
in a WROOM-32 or WROVER module, an ESP32-PICO, a DevKitC) firmware on
[lcgamboa/qemu](https://github.com/lcgamboa/qemu)'s `esp32-picsimlab` machine,
the same fork and commit as [esp32c3-qemu](../esp32c3-qemu/). The machine
models the GPIO, IOMUX, I2C, LEDC, SPI and ADC peripherals and reports them
through a callback table. It is built as `libqemu-xtensa.so`;
`heypcb-esp32-sidecar` (`sidecar.c`) loads it, runs a flash image, and speaks
esp32c3-qemu's line protocol on stdin/stdout, line for line, so one client
drives both.

| | |
|---|---|
| Simulates | ESP32 (classic), a 4 MB DIO flash image (bootloader at 0x1000) |
| Upstream | lcgamboa/qemu, branch `picsimlab-esp32`, commit [`64aed503e34a7f52f648374d7d5706ca6d71b606`](https://github.com/lcgamboa/qemu/tree/64aed503e34a7f52f648374d7d5706ca6d71b606), codeload tarball sha256 `9708818ee7b5861eaa984868a115e081e06e4b8b1fb9cfc874170511821df45c` |
| Base | `ubuntu:24.04@sha256:224a1869083a311ef3f13648a154ba79832fbef6364d31493642ca03082da254` |
| Tags | `1.0.0`, `64aed503e34a` |
| Platforms | linux/amd64, linux/arm64 |
| Files | `/opt/heypcb-esp32/`: `heypcb-esp32-sidecar`, `libqemu-xtensa.so`, `esp32-v3-rom.bin`, `esp32-v3-rom-app.bin`, `COPYING`, `LICENSE`, `SOURCE.txt`, `sidecar.c` |
| User | `10001:10001` (`sim`) |

## Run it

    docker run --rm -i --network none -v "$PWD:/work" ghcr.io/heypcb/esp32-qemu:1.0.0 \
      /opt/heypcb-esp32/libqemu-xtensa.so /opt/heypcb-esp32 /work/flash.bin

The protocol is [esp32c3-qemu's](../esp32c3-qemu/README.md#the-protocol).

## What the machine models, and what it does not

- GPIO0..31 as outputs: each write and each direction change is a `G` / `D`
  line. **GPIO32 and GPIO33 driven as outputs are not reported**: the
  machine's GPIO block models `GPIO_OUT_REG`/`GPIO_ENABLE_REG` and not their
  `…1` halves (`hw/gpio/esp32_gpio.c` at the pinned commit).
- GPIO0..39 as inputs (`P <gpio> <level>`), GPIO34..39 included.
- GPIO20, 24 and 28..31 do not exist on the chip and are not mapped.
- UART0..2 as `U` lines; I2C0/1 answered synchronously by the driver.
- No Wi-Fi or Bluetooth: `-nic none`, and no radio model is reached.
- Like the C3's machine, no pull-ups: write a button's idle level first.
- **The clock rate.** The machine runs two Xtensa cores in TCG on one host
  thread, so the sidecar counts time at `-icount shift=4`: 62.5 M
  instructions per simulated second, where the chip runs 240 MHz. A busy
  `loop()` then costs about 2.4 s of host CPU per simulated second on a
  loaded x86-64 machine (3.1–4.1 s at shift=3, 1.35 s at shift=5; measured
  2026-11-02). Timers, `millis()` and `CCOUNT` follow the virtual clock and
  stay exact (the starter's 250 ms blink is 250.0 ms of machine time); code
  timed by counting instructions (a NOP delay, bit-banged timing) runs about
  3.8x slow.
- The program's start moved between runs of one image (853 or 889 ms to its
  first pin at shift=3), so a run is not deterministic across runs.

## The ROM images

The machine boots from `esp32-v3-rom.bin` (the PRO CPU's) and
`esp32-v3-rom-app.bin` (the APP CPU's), loaded from the `-L` directory. They
are in the fork's `pc-bios/`, checked by sha256 in the `Dockerfile`, and
byte-identical to [espressif/qemu](https://github.com/espressif/qemu)'s
(added in its commit `c592afedeafaf1189c44b85c767367ce6814e34f`, 2019-09-27,
"hw/xtensa: add ESP32 machine"): Espressif's ESP32 ECO3 mask ROM code, which
Espressif distributes with its QEMU for loading into the emulated chip, with
no separate licence stated for them there.

## Fixed arguments, and no sockets

The sidecar's QEMU arguments are fixed in `sidecar.c` and never taken from a
caller:

    -M esp32-picsimlab -nographic -nodefaults -L <rom dir> -drive file=<flash>,if=mtd,format=raw
    -global driver=timer.esp32.timg,property=wdt_disable,value=true -icount shift=4,sleep=off -nic none

Measured with these arguments and no network: `strace -f -e trace=%network`
over an 8 s run recorded no network system call, and 20 samples of the
sidecar's file descriptors while the firmware printed found no socket.

## Licence and source

QEMU and lcgamboa's fork are GPL-2.0; `sidecar.c` and `build_lib.sh` are
GPL-2.0-or-later. The image carries QEMU's `COPYING` and `LICENSE`, the
sidecar's source, and `SOURCE.txt`, which names the exact QEMU source above.
The corresponding source is that commit plus this folder; to rebuild the
image from it, see the repository's [Rebuild](../README.md#rebuild).

## Smoke test

`tools/build.sh esp32-qemu` builds both architectures and boots
`firmware/esp32-fw-probe.flash.bin.gz` on each (LED GPIO2, button GPIO0,
UART0): `boot esp32` and the `led=` lines on UART0, the LED pin toggling every
250 ms of the machine's clock, one `pressed`/`released` pair after a press at
1.0–1.5 s.
