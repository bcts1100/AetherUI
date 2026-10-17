cmake_minimum_required(VERSION 3.20)
project(AetherSleekPackValidation LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
get_filename_component(AETHER_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
add_executable(AetherSleekPackValidation
  validate.cpp
  "${AETHER_ROOT}/src/core/model.cpp"
  "${AETHER_ROOT}/src/core/tiling.cpp"
  "${AETHER_ROOT}/src/core/extensions.cpp")
target_include_directories(AetherSleekPackValidation PRIVATE "${AETHER_ROOT}/src")
if(MSVC)
  target_compile_options(AetherSleekPackValidation PRIVATE /W4 /permissive- /EHsc /utf-8)
endif()
enable_testing()
add_test(NAME SleekPacks COMMAND AetherSleekPackValidation "${CMAKE_CURRENT_SOURCE_DIR}/..")
