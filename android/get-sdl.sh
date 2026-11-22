#!/bin/sh
# Download SDL's official prebuilt Android package and put its AAR where Gradle expects it.
#
# The AAR holds SDL's Java (SDLActivity and the rest) and libSDL3.so already built for every
# Android ABI, so nothing of SDL is compiled here. It is a release asset of the SDL project:
#   https://github.com/libsdl-org/SDL/releases/download/release-X.Y.Z/SDL3-devel-X.Y.Z-android.zip
#
# The version is in android/SDL_VERSION. The checksum of the zip is in android/SDL_SHA256; if that
# file is empty the checksum is printed so it can be pinned, and if it is not the download has to
# match it - a different file under the same name is refused rather than built into the app.
set -e
here="$(cd "$(dirname "$0")" && pwd)"
ver="$(tr -d ' \n\r' < "$here/SDL_VERSION")"
want="$(tr -d ' \n\r' < "$here/SDL_SHA256" 2>/dev/null || true)"
out="$here/app/libs"
aar="$out/SDL3-$ver.aar"

if [ -f "$aar" ]; then
    echo "SDL $ver AAR already at $aar"
    exit 0
fi

url="https://github.com/libsdl-org/SDL/releases/download/release-$ver/SDL3-devel-$ver-android.zip"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
echo "downloading $url"
curl -fL --retry 3 -o "$tmp/sdl.zip" "$url"

got="$(sha256sum "$tmp/sdl.zip" | cut -d' ' -f1)"
echo "sha256 of the zip: $got"
if [ -n "$want" ] && [ "$want" != "$got" ]; then
    echo "does not match android/SDL_SHA256 ($want) - refusing to use it" >&2
    exit 1
fi
[ -n "$want" ] || echo "android/SDL_SHA256 is empty: not pinned yet"

unzip -q -o "$tmp/sdl.zip" -d "$tmp/x"
found="$(find "$tmp/x" -name 'SDL3-*.aar' | head -n 1)"
if [ -z "$found" ]; then
    echo "no SDL3-*.aar in the download; it contains:" >&2
    find "$tmp/x" -type f >&2
    exit 1
fi
mkdir -p "$out"
cp "$found" "$aar"
echo "SDL $ver -> $aar"
