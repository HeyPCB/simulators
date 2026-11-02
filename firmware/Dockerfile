# Builds fw-probe (firmware/fw-probe) for every chip the images here run, and
# merges each build into the full flash image the simulator boots, exactly as
# `esptool merge-bin --pad-to-size` lays it out. Run by firmware/build.sh:
#
#   docker build --output type=local,dest=firmware/out firmware
#
# Paths inside are neutral (/fw, /pio-core) so nothing of the machine that
# built it ends up in the firmware's strings. Needs the network (PlatformIO
# downloads the pinned platform and toolchains); the result is two files.

FROM ubuntu:24.04@sha256:224a1869083a311ef3f13648a154ba79832fbef6364d31493642ca03082da254 AS build
ARG PLATFORMIO_VERSION=6.1.18
ENV DEBIAN_FRONTEND=noninteractive \
    PLATFORMIO_CORE_DIR=/pio-core \
    PLATFORMIO_NO_ANSI=1 \
    PLATFORMIO_SETTING_ENABLE_TELEMETRY=No
RUN apt-get update && apt-get install -y --no-install-recommends ca-certificates python3 python3-venv \
    && rm -rf /var/lib/apt/lists/* \
    && python3 -m venv /pio \
    && /pio/bin/pip install --no-cache-dir "platformio==${PLATFORMIO_VERSION}"
COPY fw-probe /fw/fw-probe
WORKDIR /fw/fw-probe
RUN /pio/bin/pio run -e esp32c3 -e esp32
# Merge each chip's pieces into one flash image the size the machine models:
# the bootloader at 0x0 on the C3 and 0x1000 on the classic ESP32, the
# partition table at 0x8000, Arduino's boot_app0.bin at 0xe000, the app at 0x10000.
RUN set -e; mkdir /out; \
    B=/pio-core/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin; \
    ESPTOOL="/pio/bin/python /pio-core/packages/tool-esptool/esptool.py"; \
    [ -f /pio-core/packages/tool-esptool/esptool.py ] || ESPTOOL="/pio-core/packages/tool-esptool/esptool"; \
    for env in esp32c3:esp32c3:0x0 esp32:esp32:0x1000; do \
      e=${env%%:*}; rest=${env#*:}; chip=${rest%%:*}; boot=${rest#*:}; d=.pio/build/$e; \
      $ESPTOOL --chip $chip merge-bin -o /out/$e-fw-probe.flash.bin --pad-to-size 4MB \
        --flash-mode dio --flash-size 4MB \
        $boot $d/bootloader.bin 0x8000 $d/partitions.bin 0xe000 $B 0x10000 $d/firmware.bin; \
    done; \
    ls -la /out
# What PlatformIO resolved, for the record (firmware/PACKAGES.txt).
RUN /pio/bin/pio --version > /out/PACKAGES.txt \
    && /pio/bin/pio pkg list -e esp32c3 -e esp32 >> /out/PACKAGES.txt

FROM scratch
COPY --from=build /out/ /
