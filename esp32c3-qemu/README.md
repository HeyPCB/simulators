# esp32c3-qemu — the ESP32-C3 simulator

`ghcr.io/heypcb/esp32c3-qemu` runs ESP32-C3 (RISC-V) firmware on
[lcgamboa/qemu](https://github.com/lcgamboa/qemu)'s `esp32c3-picsimlab`
machine. That machine models the C3's GPIO, IOMUX, I2C, LEDC and SARADC and
reports them through a callback table; Espressif's own QEMU models none of
the I2C, ADC, PWM or GPIO input for the C3. The machine is built as
`libqemu-riscv32.so`, and `heypcb-c3-sidecar` (`sidecar.c`) loads it, runs a
flash image, and turns the callbacks into a line protocol on stdin/stdout.
Every device model (a sensor on the I2C bus, a display) lives in whatever
drives the sidecar, never in this process.

| | |
|---|---|
| Simulates | ESP32-C3, a 4 MB DIO flash image (bootloader at 0x0) |
| Upstream | lcgamboa/qemu, branch `picsimlab-esp32`, commit [`64aed503e34a7f52f648374d7d5706ca6d71b606`](https://github.com/lcgamboa/qemu/tree/64aed503e34a7f52f648374d7d5706ca6d71b606), codeload tarball sha256 `9708818ee7b5861eaa984868a115e081e06e4b8b1fb9cfc874170511821df45c` |
| Base | `ubuntu:24.04@sha256:224a1869083a311ef3f13648a154ba79832fbef6364d31493642ca03082da254` |
| Tags | `1.0.0`, `64aed503e34a` |
| Platforms | linux/amd64, linux/arm64 |
| Files | `/opt/heypcb-c3/`: `heypcb-c3-sidecar`, `libqemu-riscv32.so`, `esp32c3-rom.bin`, `COPYING`, `LICENSE`, `SOURCE.txt`, `sidecar.c` |
| User | `10001:10001` (`sim`) |

## Run it

    docker run --rm -i --network none -v "$PWD:/work" ghcr.io/heypcb/esp32c3-qemu:1.0.0 \
      /opt/heypcb-c3/libqemu-riscv32.so /opt/heypcb-c3 /work/flash.bin

The flash file must be writable by uid 10001 (QEMU writes flash).

## The protocol

Events on stdout, one per line; `t` is microseconds of the machine's own
clock (`QEMU_CLOCK_VIRTUAL`):

    R <t>                              ready: the machine is built, the clock starts
    T <t>                              the clock, every 50 ms of wall time
    G <gpio> <0|1> <t>                 the firmware drove a pin
    D <gpio> <in|out> <t>              a pin's direction changed
    U <uart> <hex bytes> <t>           serial output, flushed at newline or 64 bytes
    I <seq> <bus> <addr> <kind> <data> <t>   an I2C event (kinds: 0 START_RECV,
                                       1 START_SEND, 2 START_SEND_ASYNC, 3 FINISH,
                                       4 NACK, 5 WRITE, 6 READ); kinds 0, 1, 2, 6
                                       wait for an answer

Commands on stdin:

    A <seq> <value>    answer question <seq>: START 0 = ACK, nonzero = NACK; READ = the byte
    P <gpio> <0|1>     drive an input pin
    V <channel> <raw>  an ADC channel's value
    Q                  stop (end of stdin stops too)

A question unanswered for 2 s of wall time takes its default (a START is
NACKed, a READ answers 0xFF) and `E unanswered <seq>` says so.

**Time is the machine's own.** `-icount shift=3,sleep=off` counts it in
instructions (8 ns each): a run is deterministic, a callback waiting on its
driver costs the firmware nothing, and idle waits are skipped, so a batch run
goes faster than real time.

**The machine models no pull-ups.** A button wired to ground with
`INPUT_PULLUP` reads LOW from boot unless the driver writes its idle level
(`P <gpio> 1`) before the firmware reads it.

## Fixed arguments, and no sockets

The library carries QEMU's slirp networking, socket chardevs and gdb stub,
reachable only through command-line options. The sidecar's QEMU arguments
are fixed in `sidecar.c` and never taken from a caller:

    -M esp32c3-picsimlab -nographic -nodefaults -L <rom dir> -drive file=<flash>,if=mtd,format=raw
    -global driver=timer.esp32c3.timg,property=wdt_disable,value=true -icount shift=3,sleep=off -nic none

The flash path's commas are doubled (QEMU's escape), so a path stays one
value; a path over 1024 bytes is refused. Measured with these arguments and no
network: `strace -f -e trace=%network` over an 8 s run recorded no network
system call, and 20 samples of the sidecar's file descriptors while the
firmware printed found no socket.

## Licence and source

QEMU and lcgamboa's fork are GPL-2.0; `sidecar.c` and `build_lib.sh` are
GPL-2.0-or-later. The image carries QEMU's `COPYING` and `LICENSE`, the
sidecar's source, and `SOURCE.txt`, which names the exact QEMU source above
(commit and tarball sha256). The corresponding source is that commit plus
this folder; to rebuild the image from it, see the repository's
[Rebuild](../README.md#rebuild). `esp32c3-rom.bin` is Espressif's ESP32-C3
mask ROM as the fork ships it in `pc-bios/`.

## Smoke test

`tools/build.sh esp32c3-qemu` builds both architectures and boots
`firmware/esp32c3-fw-probe.flash.bin.gz` on each (LED GPIO8, button GPIO9,
UART0): `boot esp32c3` and the `led=` lines on UART0, the LED pin toggling
every 250.0 ms of the machine's clock, one `pressed`/`released` pair after a
press at 1.0–1.5 s.
