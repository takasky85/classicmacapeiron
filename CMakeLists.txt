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
# release-3.2.10 steered the shooter backwards on Linux (#33), and it was SDL's bug, not ours: its
# X11 XInput2 backend turns a pointer whose axes report as *absolute* - which is what you get in a
# VM, over VNC, and under XWayland - into relative motion with the sign the wrong way round, so
# both axes came out inverted. Upstream fixed it on the 3.4 branch (libsdl-org/SDL#14926).
set(APEIRON_SDL_TAG "release-3.4.20" CACHE STRING "SDL3 tag to fetch when none is installed")

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

# --------------------------------------------------------------------------------- which build
# The title screen carries a stamp so a screenshot can be matched to the archive it came from.
# APEIRON_BUILD_NAME is the GitHub release's tag and APEIRON_BUILD_STAMP is yymmddhhmm; the release
# workflow works the stamp out in Pacific time, which is the clock the downloads are named by. A
# build from a working copy gets this machine's local time and no name.
if(NOT DEFINED APEIRON_BUILD_STAMP OR APEIRON_BUILD_STAMP STREQUAL "")
  string(TIMESTAMP APEIRON_BUILD_STAMP "%y%m%d%H%M")
endif()
if(NOT DEFINED APEIRON_BUILD_NAME)
  set(APEIRON_BUILD_NAME "")
endif()
message(STATUS "apeiron: build ${APEIRON_BUILD_NAME} ${APEIRON_BUILD_STAMP}")

# ------------------------------------------------------------------------------------- the game
set(APEIRON_SOURCES src/main.c src/platform.c src/assets.c src/intro.c src/game.c
                    src/minifont.c src/browse.c src/diag.c src/dirs.c src/droid.c)

# The Windows executable's icon. MSVC and MinGW both compile a .rc listed as a source; it is an
# original drawing (tools/make_icons.py), not anything out of the game's own resource fork.
if(WIN32)
  list(APPEND APEIRON_SOURCES packaging/apeiron.rc)
endif()

add_executable(apeiron ${APEIRON_SOURCES})
target_compile_definitions(apeiron PRIVATE
  APEIRON_BUILD_STAMP="${APEIRON_BUILD_STAMP}"
  APEIRON_BUILD_NAME="${APEIRON_BUILD_NAME}")

if(TARGET SDL3::SDL3-static AND APEIRON_STATIC_SDL)
  target_link_libraries(apeiron PRIVATE SDL3::SDL3-static)
else()
  target_link_libraries(apeiron PRIVATE SDL3::SDL3)
endif()

# A Windows release should not open a console window behind the game.
if(WIN32 AND APEIRON_STATIC_SDL)
  set_target_properties(apeiron PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

# ------------------------------------------------------------------------------ the macOS bundle
# A .app rather than a bare executable: it carries the icon, it can be double-clicked, and it is
# what a Mac expects to be handed. The artwork is still not in it - the game looks beside the
# bundle and inside Contents/Resources for an assets folder, and asks for one if it finds neither.
if(APPLE)
  set_target_properties(apeiron PROPERTIES
    MACOSX_BUNDLE TRUE
    OUTPUT_NAME "Apeiron"
    MACOSX_BUNDLE_BUNDLE_NAME "Apeiron"
    MACOSX_BUNDLE_GUI_IDENTIFIER "io.github.takasky85.apeiron"
    MACOSX_BUNDLE_ICON_FILE "apeiron.icns"
    MACOSX_BUNDLE_BUNDLE_VERSION "${APEIRON_BUILD_STAMP}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "1.0.3"
    MACOSX_BUNDLE_COPYRIGHT "A port of Ambrosia Software's Apeiron.")
  target_sources(apeiron PRIVATE packaging/apeiron.icns)
  set_source_files_properties(packaging/apeiron.icns PROPERTIES
    MACOSX_PACKAGE_LOCATION Resources)
endif()

# ------------------------------------------------------------------------------------- assets/
# assets/ is not in the repository - it is Ambrosia's artwork, and tools/extract_assets.py makes it
# from your own copy of the game. A fresh clone therefore has no assets/ to copy, and `cmake -E
# copy_directory` on a missing directory fails, which failed the whole build after a clean compile
# with nothing but "Error copying directory" to go on. So the copy is attached only when the folder
# is there, and when it is not, the build says what to run instead and still succeeds.
if(EXISTS ${CMAKE_SOURCE_DIR}/assets)
  if(APPLE)
    # Inside the bundle, where SDL_GetBasePath points, rather than next to the executable in
    # Contents/MacOS where nothing would look for it.
    add_custom_command(TARGET apeiron POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/assets
              $<TARGET_BUNDLE_CONTENT_DIR:apeiron>/Resources/assets)
  else()
    add_custom_command(TARGET apeiron POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/assets $<TARGET_FILE_DIR:apeiron>/assets)
  endif()
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
