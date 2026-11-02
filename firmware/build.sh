#!/bin/sh
# SPDX-License-Identifier: MIT
# Rebuilds the smoke-test firmware (firmware/*.flash.bin.gz) from fw-probe in
# a container, then gzips each image without a name or time stamp and writes
# SHA256SUMS of the unzipped images. Run from the repository root.
set -eu
cd "$(dirname "$0")"
rm -rf out
docker build --output type=local,dest=out .
for f in out/*.flash.bin; do
  gzip -9 -n -c "$f" > "$(basename "$f").gz"
done
( cd out && sha256sum *.flash.bin ) > SHA256SUMS
cp out/PACKAGES.txt PACKAGES.txt
cat SHA256SUMS
rm -rf out
