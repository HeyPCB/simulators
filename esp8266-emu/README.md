# esp8266-emu

The ESP8266 (Xtensa LX106; ESP-12E/F, ESP-01, ESP-07, WeMos D1 mini,
NodeMCU, ESP8285) for HeyPCB's Code tab:
[digitake/esp8266-emulator](https://github.com/digitake/esp8266-emulator)'s
`esp8266emu`. It is an LX106 instruction interpreter in C, with the mask
ROM's entry points written in C (no ROM dump), and GPIO0–16, IO_MUX, UART0/1,
the FRC timers, SPI flash and the watchdog. It boots an unmodified
Arduino-ESP8266 `firmware.bin` (eboot, then the app, from 0x0).

| | |
|---|---|
| Upstream | [`c4c840d59b60`](https://github.com/digitake/esp8266-emulator/tree/c4c840d59b6081c01cab56c6079fc089243c294d), codeload tarball sha256 `dfb9a2025f0d4c560eb000d75403cbeb49991d7efce789acc1706db8dcf46297` |
| HeyPCB's patch | `uart-times.patch`: each `uart` event carries `vt_us`, the emulated time of its first byte, and a chunk ends at a newline, so every serial line has the machine's own time (upstream flushes serial every 10 ms of HOST time, with no time) |
| Built | `make build/esp8266emu`: the plain runner, without libslirp. The emulator's optional virtual Wi-Fi (`make network`) is never built, so the binary imports no socket call; the build checks that with `nm` |
| Files | `/opt/heypcb-esp8266/esp8266emu`, `LICENSE`, `NOTICE.md`, `uart-times.patch`, `SOURCE.txt` |
| User | `10001:10001`; no network needed or used |
| Licence | MIT (the emulator and the patch) |

## Running it

    esp8266emu --flash firmware.bin --speed 0 --max-seconds 3 --no-stdin --script inputs.txt

- `--speed 0` runs as fast as the host allows, and `--max-seconds` counts
  EMULATED seconds.
- Inputs come from a timed script: `<emulated ms> in <gpio> <0|1|-1>` drives a
  pin LOW, HIGH or releases it, at that time of the machine's clock.
  `rx <uart> <hex>` sends serial bytes.
- stdout is one JSON object a line:
  - `edges`: `[t_us, gpio, level, mode]` for every pin change;
  - `uart`: `u`, `vt_us`, `d` as hex;
  - `stage`: rom, eboot, app, sdk, setup, loop or halted;
  - `halt`: why execution stopped;
  - `log`, and `stat` (host speed, the only line that varies from run to run).

`tools/emu_smoke.py` is its smoke test. It boots
`firmware/esp8266-fw-probe.flash.bin.gz` as uid 10001, with no network, a
read-only root and no capabilities, presses GPIO0 from 1.0 to 1.5 s, and
checks:
- `boot esp8266` and the `led=` lines;
- GPIO2's edges, 250 ms apart;
- `pressed` and `released` 0–60 ms after the applied edges;
- the same edges and lines in a second run.

## What it is, and is not

- **Time**: one instruction a cycle at 80 MHz, counted, not paced. `millis()`
  and `delay()` keep their times. A run is deterministic: the same image and
  script give the same edges and serial lines at the same microseconds, run
  after run. The CPU's real stalls and flash waits are not modelled, and flash
  writes are instant.
- **Not modelled**:
  - the radio: Wi-Fi scans find nothing, and the SDK's PHY calls log and
    return 0;
  - the ADC's real input;
  - an I2C or SPI device: the core's `Wire` bit-bangs GPIOs, and no device
    answers.
- An unknown ROM entry logs and returns 0 (`--strict` halts instead). The ROM
  services follow Arduino-ESP8266 3.1.2 and the NONOS SDK it ships;
  firmware built on another SDK may call an entry it does not implement.
