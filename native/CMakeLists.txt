cmake_minimum_required(VERSION 3.20)
project(comet_crash_native CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

# SDL3 is the host platform layer: window, keyboard/mouse, gamepads,
# and (in later milestones) OpenGL context + audio. Prefer its CMake
# package; fall back to pkg-config.
find_package(SDL3 QUIET)
if(NOT SDL3_FOUND)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(SDL3 REQUIRED IMPORTED_TARGET sdl3)
  add_library(SDL3::SDL3 ALIAS PkgConfig::SDL3)
endif()

# The host executable.
add_executable(comet_native
  src/main.cpp
  src/input_mapper.cpp
  src/sdl_input.cpp
  src/text_table.cpp
  src/menu_model.cpp)
target_link_libraries(comet_native PRIVATE SDL3::SDL3)
if(MSVC)
  target_compile_options(comet_native PRIVATE /W4)
else()
  target_compile_options(comet_native PRIVATE -Wall -Wextra)
endif()

# Headless unit test for the input merge logic (no SDL needed: the
# mapper is deliberately SDL-free).
enable_testing()
add_executable(input_mapper_test
  tests/input_mapper_test.cpp
  src/input_mapper.cpp)
add_test(NAME input_mapper COMMAND input_mapper_test)

# Headless unit test for the menu state machine (SDL-free by design).
add_executable(menu_model_test
  tests/menu_model_test.cpp
  src/menu_model.cpp
  src/text_table.cpp)
add_test(NAME menu_model COMMAND menu_model_test)
