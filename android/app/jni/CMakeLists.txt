# The Android build. SDLActivity loads libSDL3.so and then libmain.so, so the game has to come
# out as a shared library called exactly "main" rather than as an executable.
cmake_minimum_required(VERSION 3.16)
project(apeiron_android C)

set(SDL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../SDL")
if(NOT EXISTS "${SDL_DIR}/CMakeLists.txt")
  message(FATAL_ERROR "no SDL at ${SDL_DIR} - run android/fetch-sdl.sh first")
endif()

set(SDL_SHARED ON  CACHE BOOL "" FORCE)    # SDLActivity dlopens it by name
set(SDL_STATIC OFF CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
add_subdirectory("${SDL_DIR}" sdl)

set(GAME "${CMAKE_CURRENT_SOURCE_DIR}/../../../src")
add_library(main SHARED
  ${GAME}/main.c ${GAME}/platform.c ${GAME}/assets.c ${GAME}/intro.c ${GAME}/game.c
  ${GAME}/minifont.c ${GAME}/browse.c)

target_include_directories(main PRIVATE ${GAME})
target_compile_definitions(main PRIVATE
  APEIRON_BUILD_STAMP="${APEIRON_BUILD_STAMP}"
  APEIRON_BUILD_NAME="${APEIRON_BUILD_NAME}")
target_link_libraries(main PRIVATE SDL3::SDL3)
