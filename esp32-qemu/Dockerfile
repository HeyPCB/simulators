# SPDX-License-Identifier: MIT
# esp32-qemu: lcgamboa/qemu's esp32-picsimlab machine (the classic ESP32,
# Xtensa LX6) built as libqemu-xtensa.so, and heypcb-esp32-sidecar, which runs
# an ESP32 firmware image on it and speaks the esp32c3-qemu sidecar's line
# protocol on stdin/stdout (README.md).
#
#   tools/build.sh esp32-qemu              # both architectures, smoke-tested
#
# The build fetches the pinned source (and meson's wraps), so it needs the
# network; the image runs with none. GPLv2 (QEMU) and GPL-2.0-or-later (the
# sidecar): /opt/heypcb-esp32 carries COPYING, LICENSE and SOURCE.txt.

# Ubuntu 24.04 by digest (an index: amd64 and arm64). The library links the
# distribution's glib, pixman, gcrypt, slirp and fdt, so whoever copies
# /opt/heypcb-esp32 out runs it on this same base.
FROM ubuntu:24.04@sha256:224a1869083a311ef3f13648a154ba79832fbef6364d31493642ca03082da254 AS build
# The same fork and commit as esp32c3-qemu: one fork, both machines.
ARG QEMU_FORK_COMMIT=64aed503e34a7f52f648374d7d5706ca6d71b606
ARG QEMU_FORK_SHA256=9708818ee7b5861eaa984868a115e081e06e4b8b1fb9cfc874170511821df45c
# The ROM images the machine loads from -L, checked by content (byte-identical
# to espressif/qemu's pc-bios/, added there in c592afed, 2019-09-27).
ARG ROM_SHA256=9115bbcd03da9f54d3baca5ad94d588a43da54ed332347137ba3dc9010df258b
ARG ROM_APP_SHA256=6d3240e7cdb1ec9c1d683515e0a8578e9fb8ece3e7736cb9a996512c656e8442
ARG JOBS=4
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
      build-essential ca-certificates curl python3 python3-venv ninja-build pkg-config \
      libglib2.0-dev libpixman-1-dev libgcrypt20-dev libslirp-dev zlib1g-dev libfdt-dev \
      flex bison perl xz-utils bc git \
    && rm -rf /var/lib/apt/lists/*
RUN curl -fsSL -o /tmp/qemu.tar.gz "https://codeload.github.com/lcgamboa/qemu/tar.gz/${QEMU_FORK_COMMIT}" \
    && echo "${QEMU_FORK_SHA256}  /tmp/qemu.tar.gz" | sha256sum -c - \
    && mkdir /src && tar -xzf /tmp/qemu.tar.gz -C /src --strip-components=1 && rm /tmp/qemu.tar.gz \
    && printf '%s  %s\n%s  %s\n' "${ROM_SHA256}" /src/pc-bios/esp32-v3-rom.bin "${ROM_APP_SHA256}" /src/pc-bios/esp32-v3-rom-app.bin | sha256sum -c -
COPY build_lib.sh /build_lib.sh
RUN JOBS="${JOBS}" sh /build_lib.sh
COPY sidecar.c /sidecar.c
RUN cc -O2 -Wall -Wextra -o /heypcb-esp32-sidecar /sidecar.c -ldl -lpthread
RUN mkdir -p /opt/heypcb-esp32 \
    && cp /src/build/libqemu-xtensa.so /heypcb-esp32-sidecar /opt/heypcb-esp32/ \
    && cp /src/pc-bios/esp32-v3-rom.bin /src/pc-bios/esp32-v3-rom-app.bin /opt/heypcb-esp32/ \
    && cp /src/COPYING /src/LICENSE /opt/heypcb-esp32/ \
    && cp /sidecar.c /opt/heypcb-esp32/sidecar.c \
    && strip --strip-unneeded /opt/heypcb-esp32/libqemu-xtensa.so /opt/heypcb-esp32/heypcb-esp32-sidecar \
    && printf '%s\n' \
      "QEMU: https://github.com/lcgamboa/qemu commit ${QEMU_FORK_COMMIT} (sha256 of the codeload tarball ${QEMU_FORK_SHA256}), GPL-2.0." \
      "Sidecar: sidecar.c beside this file, from https://github.com/HeyPCB/simulators (esp32-qemu/), GPL-2.0-or-later." \
      "ROM: esp32-v3-rom.bin (sha256 ${ROM_SHA256}) and esp32-v3-rom-app.bin (sha256 ${ROM_APP_SHA256}), pc-bios/ of the" \
      "  QEMU source above; byte-identical to https://github.com/espressif/qemu pc-bios/ (commit c592afedeafaf1189c44b85c767367ce6814e34f)." \
      "  Espressif's ESP32 (ECO3) mask ROM code, as Espressif ships it with its QEMU; no separate licence is stated for them there." \
      "Rebuild: https://github.com/HeyPCB/simulators#rebuild" \
      > /opt/heypcb-esp32/SOURCE.txt \
    && chmod -R a+rX,go-w /opt/heypcb-esp32

# The image: the same base, only the run-time libraries, a non-root user.
FROM ubuntu:24.04@sha256:224a1869083a311ef3f13648a154ba79832fbef6364d31493642ca03082da254
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
      libglib2.0-0t64 libpixman-1-0 libgcrypt20 libslirp0 libfdt1 zlib1g \
    && apt-get clean && rm -rf /var/lib/apt/lists/* /var/cache/apt/*.bin /var/log/apt /var/log/dpkg.log \
    && groupadd --system --gid 10001 sim \
    && useradd --system --uid 10001 --gid 10001 --no-create-home --home-dir /nonexistent --shell /usr/sbin/nologin sim
COPY --from=build /opt/heypcb-esp32 /opt/heypcb-esp32
ARG VERSION=1.0.0
ARG REVISION=unknown
ARG QEMU_FORK_COMMIT=64aed503e34a7f52f648374d7d5706ca6d71b606
LABEL org.opencontainers.image.title="esp32-qemu" \
      org.opencontainers.image.description="ESP32 (classic) simulator: lcgamboa/qemu's esp32-picsimlab machine (libqemu-xtensa.so) behind heypcb-esp32-sidecar, a GPIO/UART/I2C line protocol on stdin/stdout. Files in /opt/heypcb-esp32." \
      org.opencontainers.image.version="${VERSION}" \
      org.opencontainers.image.revision="${REVISION}" \
      org.opencontainers.image.source="https://github.com/HeyPCB/simulators" \
      org.opencontainers.image.url="https://github.com/HeyPCB/simulators/tree/main/esp32-qemu" \
      org.opencontainers.image.licenses="GPL-2.0-only AND GPL-2.0-or-later" \
      org.opencontainers.image.vendor="HeyPCB" \
      org.opencontainers.image.base.name="docker.io/library/ubuntu:24.04@sha256:224a1869083a311ef3f13648a154ba79832fbef6364d31493642ca03082da254" \
      ai.heypcb.upstream.source="https://github.com/lcgamboa/qemu/tree/${QEMU_FORK_COMMIT}" \
      ai.heypcb.upstream.revision="${QEMU_FORK_COMMIT}" \
      ai.heypcb.upstream.license="GPL-2.0-only"
USER 10001:10001
ENTRYPOINT ["/opt/heypcb-esp32/heypcb-esp32-sidecar"]
