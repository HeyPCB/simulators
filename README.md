# HeyPCB simulators

HeyPCB's open simulator images: the microcontroller simulators
[HeyPCB](https://heypcb.ai)'s Code tab runs a board's firmware on, each
built from pinned upstream source, smoke-tested with a starter firmware, and
published as a public package. One folder per image.

| Image | Simulates | Upstream (pinned) | Licence | Tags | Platforms |
|---|---|---|---|---|---|
| [`ghcr.io/heypcb/esp32c3-qemu`](https://github.com/orgs/HeyPCB/packages/container/package/esp32c3-qemu) | ESP32-C3 (RISC-V): GPIO in/out, UART, I2C | [lcgamboa/qemu `64aed503e34a`](https://github.com/lcgamboa/qemu/tree/64aed503e34a7f52f648374d7d5706ca6d71b606) (`esp32c3-picsimlab`) | GPL-2.0 (QEMU), GPL-2.0-or-later (sidecar) | `1.0.0`, `64aed503e34a` | amd64, arm64 |
| [`ghcr.io/heypcb/esp32-qemu`](https://github.com/orgs/HeyPCB/packages/container/package/esp32-qemu) | ESP32 classic (Xtensa LX6): GPIO in/out, UART, I2C | [lcgamboa/qemu `64aed503e34a`](https://github.com/lcgamboa/qemu/tree/64aed503e34a7f52f648374d7d5706ca6d71b606) (`esp32-picsimlab`) | GPL-2.0 (QEMU), GPL-2.0-or-later (sidecar) | `1.0.0`, `64aed503e34a` | amd64, arm64 |

Tags are never only `latest`: each image is tagged with its version and with
the upstream commit it was built from, and should be pulled by digest.

## Layout

    <image>/Dockerfile     the image: upstream source pinned by commit and sha256, base pinned by digest
    <image>/build_lib.sh   how the upstream library is built (GPL-2.0-or-later)
    <image>/sidecar.c      the program the image runs (GPL-2.0-or-later)
    <image>/image.env      version, tags, platforms, and how the smoke test drives it
    <image>/README.md      what it simulates, its protocol, its licence and source
    firmware/              fw-probe, the starter firmware the smoke tests boot, and its build
    tools/build.sh         build every platform, smoke-test each, push
    tools/sidecar_smoke.py the smoke test (Python standard library + docker)
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

The results of the last run are below.
