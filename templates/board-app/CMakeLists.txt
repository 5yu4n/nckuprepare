cmake_minimum_required(VERSION 3.13)

# 板子是 RP2350B：用自訂 board header（boards/exam_rp2350b.h）。
# 若考場已給 CMakeLists，就只抄這三行 + target 的設定。
set(PICO_BOARD_HEADER_DIRS ${CMAKE_CURRENT_LIST_DIR}/boards)
set(PICO_BOARD exam_rp2350b)
# 要跑 RISC-V 核心就在 cmake 時加 -DPICO_PLATFORM=rp2350-riscv（預設 Arm）

include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)
project(board_app C CXX ASM)
set(CMAKE_C_STANDARD 11)
pico_sdk_init()

add_executable(board_app main.c)
pico_generate_pio_header(board_app ${CMAKE_CURRENT_LIST_DIR}/ws2812.pio)
pico_generate_pio_header(board_app ${CMAKE_CURRENT_LIST_DIR}/quadrature_encoder.pio)

# cmake -DMODE=1 .. → 掃輸入腳；-DMODE=2 → 掃 WS2812 腳；不給 = 示範
if (DEFINED MODE)
    target_compile_definitions(board_app PRIVATE MODE=${MODE})
endif()
target_compile_options(board_app PRIVATE -Wall -Wextra)

target_link_libraries(board_app PRIVATE
    pico_stdlib hardware_pio hardware_pwm hardware_i2c hardware_spi
    hardware_adc hardware_uart hardware_clocks)

# printf 同時走 USB（板上 USB1）與 UART0（板上 "UART 0" USB-C）
pico_enable_stdio_usb(board_app 1)
pico_enable_stdio_uart(board_app 1)
pico_add_extra_outputs(board_app)
