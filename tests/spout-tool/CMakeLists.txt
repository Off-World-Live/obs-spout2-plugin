cmake_minimum_required(VERSION 3.20)
project(spout-tool LANGUAGES CXX)

# Synthetic Spout sender/receiver on SpoutDX (see main.cpp). Built by tests/run.ps1 -Build:
#   cmake -S tests/spout-tool -B tests/spout-tool/build -G "Visual Studio 17 2022" -A x64
#   cmake --build tests/spout-tool/build --config Release

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(SPOUT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../deps/Spout2" CACHE PATH "Spout2 checkout (submodule)")
set(SPOUT_BIN "${SPOUT_ROOT}/BUILD/Binaries/x64")

if(NOT EXISTS "${SPOUT_BIN}/SpoutDX.lib")
  message(FATAL_ERROR "SpoutDX.lib not found in ${SPOUT_BIN}; run 'git submodule update --init deps/Spout2'")
endif()

add_executable(spout-tool main.cpp)
target_include_directories(spout-tool PRIVATE
  "${SPOUT_ROOT}/SPOUTSDK/SpoutDirectX/SpoutDX"
  "${SPOUT_ROOT}/SPOUTSDK/SpoutGL")
target_compile_definitions(spout-tool PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
target_link_libraries(spout-tool PRIVATE "${SPOUT_BIN}/SpoutDX.lib" d3d11 dxgi)

# SpoutDX.dll must sit next to the exe.
add_custom_command(TARGET spout-tool POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${SPOUT_BIN}/SpoutDX.dll" "$<TARGET_FILE_DIR:spout-tool>/SpoutDX.dll")
