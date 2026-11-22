#!/bin/sh
# Put the SDL source where Gradle and CMake both expect it.
#
# The Android port needs SDL's Java as well as its C, and Gradle wants the Java present before
# it configures - so this cannot be a CMake FetchContent the way the desktop build does it.
# The tag matches APEIRON_SDL_TAG in the root CMakeLists.
set -e
TAG="${1:-release-3.4.20}"
DIR="$(dirname "$0")/SDL"
if [ -d "$DIR" ]; then
    echo "SDL already at $DIR"
    exit 0
fi
git clone --depth 1 --branch "$TAG" https://github.com/libsdl-org/SDL.git "$DIR"
echo "SDL $TAG -> $DIR"
