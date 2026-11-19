#!/bin/sh
# Build and run the headless port. Everything after the options directory is passed through as the
# assets path, and the environment variables below drive it; see tools/test/README.md.
#
#   tools/test/run.sh                       # 60 seconds of play, a frame every 3 seconds
#   SCRIPT=2 tools/test/run.sh              # page through the instruction screens instead
#   LIMIT_MS=20000 SNAP_EVERY=500 tools/test/run.sh assets
#
# The frames land in frames/ as PPM, one per present() that was sampled.
set -e

root=$(cd "$(dirname "$0")/../.." && pwd)
out=${OUT_DIR:-$root/build-headless}
assets=${1:-$root/assets}

mkdir -p "$out" frames

# -lm is needed: the stub's tiled blit uses floorf.
gcc -std=gnu11 -w -I"$root/tools/test" -I"$root/src" \
    -o "$out/apeiron_headless" "$root"/src/*.c "$root/tools/test/sdl3_stub.c" -lm

if [ ! -d "$assets" ]; then
  echo "no assets at '$assets' - make them first:" >&2
  echo "  python3 tools/extract_assets.py <the installed game folder> assets" >&2
  exit 1
fi

SCRIPT=${SCRIPT:-1} \
NOAUDIO=${NOAUDIO:-1} \
LIMIT_MS=${LIMIT_MS:-60000} \
SNAP_EVERY=${SNAP_EVERY:-3000} \
exec "$out/apeiron_headless" "$assets"
