cmake_minimum_required(VERSION 3.16)
project(apeiron_boot C)
set(CMAKE_C_STANDARD 11)
find_package(SDL3 REQUIRED CONFIG)
add_executable(apeiron_boot src/boot.c)
target_link_libraries(apeiron_boot PRIVATE SDL3::SDL3)
# copy the extracted assets next to the executable so it runs from the build folder
add_custom_command(TARGET apeiron_boot POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/assets $<TARGET_FILE_DIR:apeiron_boot>/assets)
