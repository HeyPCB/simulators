#!/bin/sh
# SPDX-License-Identifier: MIT
# Build one image for every platform it lists, smoke-test each platform with
# the starter firmware, and (with --push) push it as
# ghcr.io/heypcb/<name>:<version> and :<upstream commit>.
#
#   REVISION=<commit of this repository> tools/build.sh esp32c3-qemu [--push]
#
# Needs Docker with the containerd image store (multi-platform images) and,
# on an amd64 host, arm64 emulation (binfmt) to build and run arm64.
set -eu
NAME="$1"; shift
PUSH=0
[ "${1:-}" = "--push" ] && PUSH=1
cd "$(dirname "$0")/.."
[ -f "$NAME/image.env" ] || { echo "no $NAME/image.env"; exit 2; }
. "./$NAME/image.env"
REVISION="${REVISION:-$(git rev-parse HEAD 2>/dev/null || echo unknown)}"
IMG="ghcr.io/heypcb/$NAME"
echo "== build $IMG:$VERSION ($PLATFORMS) revision $REVISION"
docker build --platform "$PLATFORMS" --build-arg VERSION="$VERSION" --build-arg REVISION="$REVISION" \
  --build-arg JOBS="${JOBS:-4}" -t "$IMG:$VERSION" -t "$IMG:$UPSTREAM_TAG" "$NAME"
for p in $(echo "$PLATFORMS" | tr ',' ' '); do
  echo "== smoke $IMG:$VERSION on $p"
  python3 tools/sidecar_smoke.py --image "$IMG:$VERSION" --platform "$p" \
    --dir "$SMOKE_DIR" --exe "$SMOKE_EXE" --lib "$SMOKE_LIB" --flash "$SMOKE_FLASH" \
    --mcu "$SMOKE_MCU" --led "$SMOKE_LED" --button "$SMOKE_BUTTON"
done
if [ "$PUSH" = 1 ]; then
  docker push "$IMG:$VERSION"
  docker push "$IMG:$UPSTREAM_TAG"
  # The registry links a multi-platform package to its repository from the
  # INDEX's annotations, not the images' labels: write the index again with
  # them, over exactly the platform images just tested and pushed.
  docker buildx imagetools create \
    --annotation "index:org.opencontainers.image.source=https://github.com/HeyPCB/simulators" \
    --annotation "index:org.opencontainers.image.description=$DESCRIPTION" \
    --annotation "index:org.opencontainers.image.licenses=$LICENSES" \
    --annotation "index:org.opencontainers.image.version=$VERSION" \
    --annotation "index:org.opencontainers.image.revision=$REVISION" \
    -t "$IMG:$VERSION" -t "$IMG:$UPSTREAM_TAG" "$IMG:$VERSION"
  docker buildx imagetools inspect "$IMG:$VERSION"
fi
