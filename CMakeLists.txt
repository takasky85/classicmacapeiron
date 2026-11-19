cmake_minimum_required(VERSION 3.16)
project(apeiron C)
set(CMAKE_C_STANDARD 11)

# ---------------------------------------------------------------------------------------- SDL3
# An installed SDL3 is used when there is one, so a developer's own build is left alone. When there
# is not - which is the case on every CI runner, and for anyone who would rather not install it -
# SDL3 is fetched at a pinned tag and built as part of this project.
#
# APEIRON_STATIC_SDL links that fetched SDL3 statically, which is what the release builds do: the
# result is one executable that needs no SDL3.dll, no MinGW runtime and no libiconv beside it. SDL
# is zlib-licensed, so static linking carries no condition beyond keeping its notice.
option(APEIRON_STATIC_SDL "Link the fetched SDL3 statically into one self-contained executable" OFF)
set(APEIRON_SDL_TAG "release-3.2.10" CACHE STRING "SDL3 tag to fetch when none is installed")

find_package(SDL3 CONFIG QUIET)
if(SDL3_FOUND)
  message(STATUS "apeiron: using the installed SDL3")
else()
  message(STATUS "apeiron: no installed SDL3 - fetching ${APEIRON_SDL_TAG}")
  include(FetchContent)
  if(APEIRON_STATIC_SDL)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
  endif()
  set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
  set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG ${APEIRON_SDL_TAG}
    GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(SDL3)
endif()

# ------------------------------------------------------------------------------------- the game
add_executable(apeiron src/main.c src/platform.c src/assets.c src/intro.c src/game.c)

if(TARGET SDL3::SDL3-static AND APEIRON_STATIC_SDL)
  target_link_libraries(apeiron PRIVATE SDL3::SDL3-static)
else()
  target_link_libraries(apeiron PRIVATE SDL3::SDL3)
endif()

# A Windows release should not open a console window behind the game.
if(WIN32 AND APEIRON_STATIC_SDL)
  set_target_properties(apeiron PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

# ------------------------------------------------------------------------------------- assets/
# assets/ is not in the repository - it is Ambrosia's artwork, and tools/extract_assets.py makes it
# from your own copy of the game. A fresh clone therefore has no assets/ to copy, and `cmake -E
# copy_directory` on a missing directory fails, which failed the whole build after a clean compile
# with nothing but "Error copying directory" to go on. So the copy is attached only when the folder
# is there, and when it is not, the build says what to run instead and still succeeds.
if(EXISTS ${CMAKE_SOURCE_DIR}/assets)
  add_custom_command(TARGET apeiron POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/assets $<TARGET_FILE_DIR:apeiron>/assets)
else()
  message(STATUS
    "apeiron: no assets/ directory yet - the game will need one to run.\n"
    "   Extract it from your own copy of Apeiron:\n"
    "     python3 tools/extract_assets.py <the installed game folder> assets\n"
    "   then build again, or pass the path at runtime: ./apeiron path/to/assets")
endif()

# --------------------------------------------------------------------------- Windows runtime DLLs
# Only needed for a dynamically linked build. A static one has nothing to copy.
if(WIN32 AND NOT APEIRON_STATIC_SDL)
  if(TARGET SDL3::SDL3-shared)
    add_custom_command(TARGET apeiron POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:SDL3::SDL3-shared> $<TARGET_FILE_DIR:apeiron> VERBATIM)
  endif()
  # Nothing here uses libiconv: it comes in with SDL3 when SDL is built against the MSYS2 or
  # MinGW one, and then SDL3.dll will not load without libiconv-2.dll beside it. CMake can work
  # out that whole chain and copy it for us, so the build folder is self-contained.
  if(NOT CMAKE_VERSION VERSION_LESS 3.21)
    add_custom_command(TARGET apeiron POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_RUNTIME_DLLS:apeiron> $<TARGET_FILE_DIR:apeiron>
      COMMAND_EXPAND_LISTS)
  endif()
endif()
