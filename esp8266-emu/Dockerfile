# SPDX-License-Identifier: MIT
# esp8266-emu: digitake/esp8266-emulator's esp8266emu, an ESP8266 (Xtensa
# LX106) emulator in C (an instruction interpreter, the mask ROM's entry
# points written in C, GPIO, UART, timers, flash, watchdog), with HeyPCB's
# patch giving each serial line the emulated time of its first byte
# (uart-times.patch). It boots an unmodified Arduino-ESP8266 firmware.bin and
# writes its pins and serial as JSON lines on stdout (README.md).
#
#   tools/build.sh esp8266-emu              # both architectures, smoke-tested
#
# The build fetches the pinned source, so it needs the network; the image runs
# with none. MIT (the emulator and the patch); no ROM dump, SDK or firmware is
# in it. /opt/heypcb-esp8266 carries LICENSE, NOTICE.md, the patch and SOURCE.txt.

# Ubuntu 24.04 by digest (an index: amd64 and arm64), the base of the other
# images here: the binary links only glibc and libm, so whoever copies
# /opt/heypcb-esp8266 out runs it on this same base.
FROM ubuntu:24.04@sha256:224a1869083a311ef3f13648a154ba79832fbef6364d31493642ca03082da254 AS build
ARG EMU_COMMIT=c4c840d59b6081c01cab56c6079fc089243c294d
ARG EMU_SHA256=dfb9a2025f0d4c560eb000d75403cbeb49991d7efce789acc1706db8dcf46297
ARG JOBS=4
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
      build-essential ca-certificates curl patch \
    && rm -rf /var/lib/apt/lists/*
RUN curl -fsSL -o /tmp/emu.tar.gz "https://codeload.github.com/digitake/esp8266-emulator/tar.gz/${EMU_COMMIT}" \
    && echo "${EMU_SHA256}  /tmp/emu.tar.gz" | sha256sum -c - \
    && mkdir /src && tar -xzf /tmp/emu.tar.gz -C /src --strip-components=1 && rm /tmp/emu.tar.gz
COPY uart-times.patch /uart-times.patch
# The plain runner only: no libslirp, so no network backend (its virtual
# Wi-Fi needs `make network`, which this image never builds). Checked below:
# the binary imports no socket call.
RUN cd /src && patch -p1 < /uart-times.patch \
    && make -j"${JOBS}" build/esp8266emu \
    && ! nm -D --undefined-only build/esp8266emu | grep -Eiw 'socket|connect|bind|listen|accept|getaddrinfo|sendto|recvfrom'
RUN mkdir -p /opt/heypcb-esp8266 \
    && cp /src/build/esp8266emu /opt/heypcb-esp8266/ \
    && strip --strip-unneeded /opt/heypcb-esp8266/esp8266emu \
    && cp /src/LICENSE /src/NOTICE.md /uart-times.patch /opt/heypcb-esp8266/ \
    && printf '%s\n' \
      "esp8266emu: https://github.com/digitake/esp8266-emulator commit ${EMU_COMMIT} (sha256 of the codeload tarball ${EMU_SHA256}), MIT (LICENSE, NOTICE.md)." \
      "Built: make build/esp8266emu (no network backend), with uart-times.patch beside this file (HeyPCB, MIT): each UART event carries" \
      "  vt_us, the emulated time of its first byte, and a chunk ends at a newline." \
      "No ROM dump, SDK library or firmware is included: the emulator implements the ROM's entry points in C." \
      "Rebuild: https://github.com/HeyPCB/simulators#rebuild" \
      > /opt/heypcb-esp8266/SOURCE.txt \
    && chmod -R a+rX,go-w /opt/heypcb-esp8266

# The image: the same base, nothing installed, a non-root user.
FROM ubuntu:24.04@sha256:224a1869083a311ef3f13648a154ba79832fbef6364d31493642ca03082da254
RUN groupadd --system --gid 10001 sim \
    && useradd --system --uid 10001 --gid 10001 --no-create-home --home-dir /nonexistent --shell /usr/sbin/nologin sim
COPY --from=build /opt/heypcb-esp8266 /opt/heypcb-esp8266
ARG VERSION=1.0.0
ARG REVISION=unknown
ARG EMU_COMMIT=c4c840d59b6081c01cab56c6079fc089243c294d
LABEL org.opencontainers.image.title="esp8266-emu" \
      org.opencontainers.image.description="ESP8266 (Xtensa LX106) simulator: digitake/esp8266-emulator's esp8266emu with HeyPCB's timed-UART patch; GPIO, UART and timers as JSON lines on stdout, inputs from a timed script. Files in /opt/heypcb-esp8266." \
      org.opencontainers.image.version="${VERSION}" \
      org.opencontainers.image.revision="${REVISION}" \
      org.opencontainers.image.source="https://github.com/HeyPCB/simulators" \
      org.opencontainers.image.url="https://github.com/HeyPCB/simulators/tree/main/esp8266-emu" \
      org.opencontainers.image.licenses="MIT" \
      org.opencontainers.image.vendor="HeyPCB" \
      org.opencontainers.image.base.name="docker.io/library/ubuntu:24.04@sha256:224a1869083a311ef3f13648a154ba79832fbef6364d31493642ca03082da254" \
      ai.heypcb.upstream.source="https://github.com/digitake/esp8266-emulator/tree/${EMU_COMMIT}" \
      ai.heypcb.upstream.revision="${EMU_COMMIT}" \
      ai.heypcb.upstream.license="MIT"
USER 10001:10001
ENTRYPOINT ["/opt/heypcb-esp8266/esp8266emu"]
