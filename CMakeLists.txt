cmake_minimum_required(VERSION 3.16)
project(apeiron C)
set(CMAKE_C_STANDARD 11)
find_package(SDL3 REQUIRED CONFIG)
add_executable(apeiron src/main.c src/platform.c src/assets.c src/intro.c src/game.c)
target_link_libraries(apeiron PRIVATE SDL3::SDL3)

add_custom_command(TARGET apeiron POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/assets $<TARGET_FILE_DIR:apeiron>/assets)
if(WIN32)
  add_custom_command(TARGET apeiron POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:SDL3::SDL3> $<TARGET_FILE_DIR:apeiron> VERBATIM)
  # Nothing here uses libiconv: it comes in with SDL3 when SDL is built against the MSYS2 or
  # MinGW one, and then SDL3.dll will not load without libiconv-2.dll beside it. CMake can work
  # out that whole chain and copy it for us, so the build folder is self-contained.
  if(NOT CMAKE_VERSION VERSION_LESS 3.21)
    add_custom_command(TARGET apeiron POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_RUNTIME_DLLS:apeiron> $<TARGET_FILE_DIR:apeiron>
      COMMAND_EXPAND_LISTS)
  endif()
endif()
