# HeyPCB simulators

HeyPCB's open simulator images: the microcontroller simulators
[HeyPCB](https://heypcb.ai)'s Code tab runs a board's firmware on, each
built from pinned upstream source, smoke-tested with a starter firmware, and
published as a public package. One folder per image.

| Image | Simulates | Upstream (pinned) | Licence | Tags | Platforms |
|---|---|---|---|---|---|
| [`ghcr.io/heypcb/esp32c3-qemu`](https://github.com/orgs/HeyPCB/packages/container/package/esp32c3-qemu) | ESP32-C3 (RISC-V): GPIO in/out, UART, I2C | [lcgamboa/qemu `64aed503e34a`](https://github.com/lcgamboa/qemu/tree/64aed503e34a7f52f648374d7d5706ca6d71b606) (`esp32c3-picsimlab`) | GPL-2.0 (QEMU), GPL-2.0-or-later (sidecar) | `1.0.0`, `64aed503e34a` | amd64, arm64 |
| [`ghcr.io/heypcb/esp32-qemu`](https://github.com/orgs/HeyPCB/packages/container/package/esp32-qemu) | ESP32 classic (Xtensa LX6): GPIO in/out, UART, I2C | [lcgamboa/qemu `64aed503e34a`](https://github.com/lcgamboa/qemu/tree/64aed503e34a7f52f648374d7d5706ca6d71b606) (`esp32-picsimlab`) | GPL-2.0 (QEMU), GPL-2.0-or-later (sidecar) | `1.0.0`, `64aed503e34a` | amd64, arm64 |
| [`ghcr.io/heypcb/esp8266-emu`](https://github.com/orgs/HeyPCB/packages/container/package/esp8266-emu) | ESP8266 / ESP8285 (Xtensa LX106): GPIO0–16 in/out, UART | [digitake/esp8266-emulator `c4c840d59b60`](https://github.com/digitake/esp8266-emulator/tree/c4c840d59b6081c01cab56c6079fc089243c294d) (`esp8266emu`, + `uart-times.patch`) | MIT | `1.0.0`, `c4c840d59b60` | amd64, arm64 |

Tags are never only `latest`: each image is tagged with its version and with
the upstream commit it was built from, and should be pulled by digest.

## Layout

    <image>/Dockerfile     the image: upstream source pinned by commit and sha256, base pinned by digest
    <image>/build_lib.sh   how the upstream library is built (GPL-2.0-or-later)
    <image>/sidecar.c      the program the image runs (GPL-2.0-or-later)
    <image>/*.patch        HeyPCB's changes to the upstream source, applied by the Dockerfile
    <image>/image.env      version, tags, platforms, and how the smoke test drives it
    <image>/README.md      what it simulates, its protocol, its licence and source
    firmware/              fw-probe, the starter firmware the smoke tests boot, and its build
    tools/build.sh         build every platform, smoke-test each, push
    tools/sidecar_smoke.py the smoke test (Python standard library + docker)
    tools/emu_smoke.py     the same smoke test for an image that runs its emulator directly (esp8266-emu)
    tools/scan.sh          the security checks run before anything is published

## Rebuild

You need Docker with the containerd image store (for multi-platform images)
and, on an x86-64 host, arm64 emulation:

    docker run --privileged --rm tonistiigi/binfmt:qemu-v10.2.3@sha256:400a4873b838d1b89194d982c45e5fb3cda4593fbfd7e08a02e76b03b21166f0 --install arm64

Then, from a checkout of this repository:

    REVISION=$(git rev-parse HEAD) tools/build.sh esp32-qemu            # build + smoke-test amd64 and arm64
    REVISION=$(git rev-parse HEAD) tools/build.sh esp32-qemu --push     # ...and push both tags
    firmware/build.sh                                                   # rebuild the smoke-test firmware

The build downloads the pinned upstream source and checks its sha256; the
images run with no network. `.github/workflows/build.yml` runs the same
script on GitHub's runners, by hand only (`workflow_dispatch`), never on a
push.

## The smoke test

`tools/build.sh` boots `firmware/<chip>-fw-probe.flash.bin.gz` in every
platform of the image it just built, as the image's non-root user, with no
network, a read-only root filesystem and every capability dropped, and checks
what the machine saw, not what the firmware claims:

1. the UART prints `boot <chip>` and at least four `led=` lines;
2. the LED pin, as the machine's GPIO model drove it, toggles every 250 ms of
   the machine's own clock (±10 %);
3. a button press applied at 1.0 s and released at 1.5 s prints exactly one
   `pressed` and one `released`, each 0–60 ms after the edge.

An image that runs its emulator directly (`SMOKE_KIND=emu` in its `image.env`) is driven by a
timed input script instead of the sidecar protocol (`tools/emu_smoke.py`), and must also give
the same edges and serial lines in a second run.

An image is pushed only after every platform passes.

## Licences

- This repository's own build glue (Dockerfiles, `tools/`, `firmware/`,
  documentation) is MIT: [LICENSE](LICENSE).
- Each image's `sidecar.c` and `build_lib.sh` are GPL-2.0-or-later: the
  sidecar loads QEMU, and the build script follows the QEMU fork's own.
- QEMU and lcgamboa's fork are GPL-2.0. **Source offer**: every image names
  its exact upstream source (repository, commit and tarball sha256) in its
  README, in its labels (`ai.heypcb.upstream.source`,
  `ai.heypcb.upstream.revision`, `ai.heypcb.upstream.license`) and in the
  `SOURCE.txt` it carries beside QEMU's `COPYING` and `LICENSE` and the
  sidecar's source; that source plus the image's folder here is the complete
  corresponding source, and [Rebuild](#rebuild) builds the image from it.
- The ESP32 and ESP32-C3 ROM images are Espressif's, as the QEMU fork ships
  them (each image's README).
- The prebuilt smoke-test firmware links Arduino-ESP32 (LGPL-2.1-or-later)
  and ESP-IDF (Apache-2.0): [firmware/README.md](firmware/README.md).

Every image carries `org.opencontainers.image.source`
(`https://github.com/HeyPCB/simulators`), `.description`, `.licenses`,
`.version` and `.revision` (the commit of this repository it was built from).

## Security

What is checked before anything here is published (`tools/scan.sh` for each
image and platform; gitleaks and trufflehog over this repository and its
full history):

- **No secrets** in the repository, its history or any image layer:
  gitleaks (`git` and `dir` modes) and trufflehog over the repository; for
  each image, `docker history --no-trunc`, its environment and labels, a
  scan of its whole filesystem for secret-bearing files (`.env`, keys,
  `.ssh`, git and Docker credentials, `.netrc`, `.npmrc`, `.pypirc`), and
  gitleaks, trufflehog and a deny-pattern scan (tokens, private keys,
  home directories, tailnet names, hosted-database and hosting URLs, plus a
  maintainer-held list of internal names that is never written here) over
  every file the image's own layers add.
- **Only the simulators**: the upstream simulator, its build glue, the
  sidecar, and the public starter firmware the smoke tests boot. No other
  HeyPCB code is here, and the repository started fresh, with none of
  another repository's history.
- **A small, non-root image**: Ubuntu 24.04 pinned by digest, only the
  run-time libraries, no compiler, build tool, git, curl, Python or package
  cache in the final stage, `USER 10001:10001`; the smoke test runs it with
  no network, a read-only root filesystem and every capability dropped.

### The last run (2026-11-02, before the repository and the `1.0.0` images were made public)

| Check | esp32c3-qemu 1.0.0 | esp32-qemu 1.0.0 |
|---|---|---|
| runs as | `10001:10001` (amd64, arm64) | `10001:10001` (amd64, arm64) |
| `docker history --no-trunc`, env, labels: deny patterns | none (both platforms) | none (both platforms) |
| secret-bearing files anywhere in the filesystem | none | none |
| compiler, build tool, git, curl, wget, Python, pip | none | none |
| apt lists, cached packages | none | none |
| gitleaks over `/opt/heypcb-*` | no leaks | no leaks |
| trufflehog over `/opt/heypcb-*` | 0 findings | 0 findings |
| deny patterns over every file in `/opt/heypcb-*` | none | none |

`tools/scan.sh` was also run on a test image planted with a `.env` file,
an AWS-style key, a tailnet host name and `USER 0`: it failed it on the
user, the history, the file, gitleaks and the deny patterns (trufflehog alone
did not flag the made-up key: the other layers are why there are five).

The repository: gitleaks (`dir` over the files; `git` over every commit) no
leaks; trufflehog over the files and the full history 0 findings; the deny
patterns, internal names included, over every commit's patch matched
nothing but the account's display name in the commit authors. The smoke-test
firmware was rebuilt in a container for this repository because the images
built earlier on a workstation carried its home directory in their strings.

### The smoke test, measured (2026-11-02)

| image | platform | serial | LED gaps (machine time) | press → `pressed`, release → `released` | wall |
|---|---|---|---|---|---|
| esp32c3-qemu 1.0.0 | amd64 | `boot esp32c3`, 11 `led=` lines | 254.6–256.0 ms | 20.1 ms, 20.7 ms | 7.5 s |
| esp32c3-qemu 1.0.0 | arm64 (emulated) | `boot esp32c3`, 11 `led=` lines | 254.6–256.0 ms | 19.7 ms, 20.0 ms | 148.3 s |
| esp32-qemu 1.0.0 | amd64 | `boot esp32`, 8 `led=` lines | 249.6, then 250.0 ms | 20.1 ms, 19.7 ms | 6.6 s |
| esp32-qemu 1.0.0 | arm64 (emulated) | `boot esp32`, 8 `led=` lines | 249.6, then 250.0 ms | 19.3 ms, 19.7 ms | 90.7 s |

arm64 was built and run under emulation on an x86-64 host; it has not been
run on arm64 hardware.

### Published

| image | tags | index digest | platforms |
|---|---|---|---|
| `ghcr.io/heypcb/esp32c3-qemu` | `1.0.0`, `64aed503e34a` | `sha256:ae483f1d9f930980610c50969290271ac37ee3f753add12efb08c5da4c14ce16` | linux/amd64 `sha256:9f710de7…`, linux/arm64 `sha256:95b31201…` |
| `ghcr.io/heypcb/esp32-qemu` | `1.0.0`, `64aed503e34a` | `sha256:39d06deede35b7b38be693856f95c307325407d0b96f2db1e83325114331df25` | linux/amd64 `sha256:f999f1f1…`, linux/arm64 `sha256:ded82b3d…` |

Both were built from commit `1394e5202b4d0a6615f0fda194cf0944aa6e06cc` of
this repository (their `org.opencontainers.image.revision`). Older tags in
the same packages (`64aed50`) predate this repository and are not built from
it.
