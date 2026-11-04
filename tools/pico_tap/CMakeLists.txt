cmake_minimum_required(VERSION 3.13)
# pico_tap: a spare Pico W as an edge recorder for the skew tests (bench
# plan 5 Nov 2026). WiFi is not used: only the RP2040 GPIO and USB.
set(PICO_BOARD pico_w)
include(pico_sdk_import.cmake)
project(pico_tap C CXX ASM)
set(CMAKE_C_STANDARD 11)
pico_sdk_init()

add_executable(pico_tap main.c tap_ring.c)
target_include_directories(pico_tap PRIVATE ${CMAKE_CURRENT_LIST_DIR})
target_compile_options(pico_tap PRIVATE -Wall -Wextra)
target_link_libraries(pico_tap pico_stdlib hardware_gpio)
pico_enable_stdio_usb(pico_tap 1)
pico_enable_stdio_uart(pico_tap 0)
pico_add_extra_outputs(pico_tap)
