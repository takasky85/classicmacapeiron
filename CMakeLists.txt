cmake_minimum_required(VERSION 3.16)
project(apeiron C)
set(CMAKE_C_STANDARD 11)
find_package(SDL3 REQUIRED CONFIG)
add_executable(apeiron src/main.c src/platform.c src/assets.c src/intro.c src/game.c)
target_link_libraries(apeiron PRIVATE SDL3::SDL3)
# the startup-only build from the first pass, kept for reference
add_executable(apeiron_boot src/boot.c)
target_link_libraries(apeiron_boot PRIVATE SDL3::SDL3)
add_custom_command(TARGET apeiron POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/assets $<TARGET_FILE_DIR:apeiron>/assets)
if(WIN32)
  add_custom_command(TARGET apeiron POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:SDL3::SDL3> $<TARGET_FILE_DIR:apeiron> VERBATIM)
endif()
