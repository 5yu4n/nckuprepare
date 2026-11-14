# templates/pico-app：複製整個資料夾 → 改 main.c → 建置。
#   mkdir build && cd build
#   cmake -DPICO_PLATFORM=rp2350-riscv ..     # Arm 版：-DPICO_PLATFORM=rp2350-arm-s（板子預設用 boards/exam_rp2350b.h）
#   make -j4                                                       # 產生 app.uf2、app.dis
# 需要 pico_sdk_import.cmake：從 $PICO_SDK_PATH/external/pico_sdk_import.cmake 複製到這個資料夾。
# 考場若已給 CMakeLists.txt，照考場的，只把下面 target_link_libraries 缺的庫加進去。
cmake_minimum_required(VERSION 3.13)

# 考場板子是 RP2350B（48 GPIO）：用 boards/exam_rp2350b.h。用 pico2 的話 GPIO 30–47 不能用。
list(APPEND PICO_BOARD_HEADER_DIRS ${CMAKE_CURRENT_LIST_DIR}/boards)
if (NOT PICO_BOARD)
    set(PICO_BOARD exam_rp2350b)
endif()
if (NOT PICO_PLATFORM)
    set(PICO_PLATFORM rp2350-riscv)
endif()

include(pico_sdk_import.cmake)
project(app C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
pico_sdk_init()

add_executable(app main.c)

# 有 PIO 程式才打開（main.c 裡 #include "xform.pio.h"）
# pico_generate_pio_header(app ${CMAKE_CURRENT_LIST_DIR}/xform.pio)

target_link_libraries(app
    pico_stdlib
    pico_multicore        # dual_core.h
    hardware_dma          # dma_util.h
    hardware_pio
    hardware_sync
    hardware_irq
    hardware_exception    # fault_report.h
)

pico_enable_stdio_usb(app 1)
pico_enable_stdio_uart(app 0)

# 題目允許改 CMake 時可試：整個程式放 RAM 跑（不受 flash / XIP 快取影響）
# pico_set_binary_type(app copy_to_ram)

pico_add_extra_outputs(app)
