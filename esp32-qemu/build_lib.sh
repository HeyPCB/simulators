#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Builds libqemu-xtensa.so from lcgamboa/qemu (the picsimlab-esp32 branch,
# pinned in the Dockerfile) as that fork's own build_libqemu-esp32.sh does,
# for the xtensa target alone: every optional QEMU feature off but TCG, the
# system emulator, gcrypt, slirp and fdt; the normal build; then the final
# link line again without system/main.o and with -shared. The ESP32-C3's
# ../esp32c3-qemu/build_lib.sh is the same recipe for riscv32, and the
# flags are kept identical so both libraries need the same run-time packages.
set -e
cd /src
target="xtensa-softmmu"
flags="$(./configure --help | perl -ne 'print if s/^  ([a-z][\w-]*) .*/\1/' | tail -n +2 | awk '{print "--disable-"$1}' ORS=' ')"
./configure --target-list=$target --extra-cflags=-fPIC $flags --enable-tcg \
  --enable-system --disable-werror --enable-gcrypt --enable-slirp --enable-fdt=system
make -j"${JOBS:-4}"
cd build
rm -f qemu-system-xtensa qemu-system-xtensa.rsp
ninja -v -d keeprsp > qemu-system-xtensa_.rsp
sed -i -n '$p' qemu-system-xtensa_.rsp
CMD=$(sed 's/-o .*//' qemu-system-xtensa_.rsp | sed 's/\[.\/.\] //g' | sed 's/@qemu-system-xtensa.rsp//g')
[ -f qemu-system-xtensa.rsp ] || cp qemu-system-xtensa_.rsp qemu-system-xtensa.rsp
sed -i 's/.*-o /-o /' qemu-system-xtensa.rsp
sed -i 's/qemu-system-xtensa.p\/system_main.c.o//g' qemu-system-xtensa.rsp
sed -i 's/-o\ qemu-system-xtensa/-shared\ -o\ libqemu-xtensa.so/g' qemu-system-xtensa.rsp
eval "$CMD @qemu-system-xtensa.rsp"
ls -la libqemu-xtensa.so
