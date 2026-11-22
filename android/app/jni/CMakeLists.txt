# The Android build. SDLActivity loads libSDL3.so and then libmain.so, so the game has to come
# out as a shared library called exactly "main" rather than as an executable.
cmake_minimum_required(VERSION 3.16)
project(apeiron_android C)

# SDL comes from the official prebuilt AAR (android/get-sdl.sh puts it in app/libs, Gradle's
# prefab support exposes it here). Nothing of SDL is compiled by this build.
find_package(SDL3 REQUIRED CONFIG)

set(GAME "${CMAKE_CURRENT_SOURCE_DIR}/../../../src")
add_library(main SHARED
  ${GAME}/main.c ${GAME}/platform.c ${GAME}/assets.c ${GAME}/intro.c ${GAME}/game.c
  ${GAME}/minifont.c ${GAME}/browse.c ${GAME}/diag.c ${GAME}/dirs.c ${GAME}/droid.c
  ${GAME}/convert.c ${GAME}/fontdata.c)

target_include_directories(main PRIVATE ${GAME})
target_compile_definitions(main PRIVATE
  APEIRON_BUILD_STAMP="${APEIRON_BUILD_STAMP}"
  APEIRON_BUILD_NAME="${APEIRON_BUILD_NAME}")
target_link_libraries(main PRIVATE SDL3::SDL3)

# Android 15 ships devices with 16KB memory pages, and a shared library laid out for 4KB pages
# will not load on one at all - the app dies before main runs. Recent NDKs do this by default;
# saying it here means the build does not depend on which NDK the runner happens to have.
target_link_options(main PRIVATE "LINKER:-z,max-page-size=16384")
