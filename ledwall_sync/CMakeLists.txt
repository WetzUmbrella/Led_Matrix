cmake_minimum_required(VERSION 3.13)
set(PICO_BOARD pico_w)
include(pico_sdk_import.cmake)
project(ledwall_sync C CXX ASM)
set(CMAKE_C_STANDARD 11)
pico_sdk_init()

add_executable(ledwall_sync main.c coordinator_loop.c join_backoff.c game_rules.c score_font.c ../common/proto/proto.c ../common/proto/ack_outbox.c)
target_include_directories(ledwall_sync PRIVATE ${CMAKE_CURRENT_LIST_DIR} ${CMAKE_CURRENT_LIST_DIR}/../common/proto)
# Poll mode: T-B2-02, OD-01 PROVISIONAL (owner Benjamin).
target_link_libraries(ledwall_sync pico_stdlib pico_cyw43_arch_lwip_poll)
# Name shown in the router's client list (hyphens, not underscores: routers reject "_" in hostnames)
target_compile_definitions(ledwall_sync PRIVATE CYW43_HOST_NAME="PICO-Main")
# A USB host that has the port open but stops reading can block printf for
# up to 500 ms (SDK 2.3.1 stdio_usb.h:33). 1 ms bounds that, so a stalled
# terminal never delays a beacon by more than one log line (T-B2-02).
target_compile_definitions(ledwall_sync PRIVATE PICO_STDIO_USB_STDOUT_TIMEOUT_US=1000)
# T-B4-05 test build: cmake -DCL_TEST_SUPPRESS_MS=<ms> ..
if(DEFINED CL_TEST_SUPPRESS_MS)
    target_compile_definitions(ledwall_sync PRIVATE CL_TEST_SUPPRESS_MS=${CL_TEST_SUPPRESS_MS})
endif()
pico_enable_stdio_usb(ledwall_sync 1)
pico_enable_stdio_uart(ledwall_sync 0)
pico_add_extra_outputs(ledwall_sync)
