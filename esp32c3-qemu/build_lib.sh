#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Builds libqemu-riscv32.so from lcgamboa/qemu (the picsimlab-esp32 branch,
# pinned in the Dockerfile) as that fork's own build_libqemu-esp32.sh does,
# for the riscv32 target alone: every optional QEMU feature off but TCG, the
# system emulator, gcrypt, slirp and fdt; the normal build; then the final
# link line again without system/main.o and with -shared.
set -e
cd /src
target="riscv32-softmmu"
flags="$(./configure --help | perl -ne 'print if s/^  ([a-z][\w-]*) .*/\1/' | tail -n +2 | awk '{print "--disable-"$1}' ORS=' ')"
./configure --target-list=$target --extra-cflags=-fPIC $flags --enable-tcg \
  --enable-system --disable-werror --enable-gcrypt --enable-slirp --enable-fdt=system
make -j"${JOBS:-4}"
cd build
rm -f qemu-system-riscv32 qemu-system-riscv32.rsp
ninja -v -d keeprsp > qemu-system-riscv32_.rsp
sed -i -n '$p' qemu-system-riscv32_.rsp
CMD=$(sed 's/-o .*//' qemu-system-riscv32_.rsp | sed 's/\[.\/.\] //g' | sed 's/@qemu-system-riscv32.rsp//g')
[ -f qemu-system-riscv32.rsp ] || cp qemu-system-riscv32_.rsp qemu-system-riscv32.rsp
sed -i 's/.*-o /-o /' qemu-system-riscv32.rsp
sed -i 's/qemu-system-riscv32.p\/system_main.c.o//g' qemu-system-riscv32.rsp
sed -i 's/-o\ qemu-system-riscv32/-shared\ -o\ libqemu-riscv32.so/g' qemu-system-riscv32.rsp
eval "$CMD @qemu-system-riscv32.rsp"
ls -la libqemu-riscv32.so
