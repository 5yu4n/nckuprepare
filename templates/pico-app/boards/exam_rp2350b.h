// 考場板子（RP2350B, 80-pin QFN）用的 board header。
// 重點：PICO_RP2350A 0 → NUM_BANK0_GPIOS = 48，GPIO 30–47 才能用；ADC 在 GPIO 40–47。
// 若用 PICO_BOARD=pico2，SDK 會當成 RP2350A（只有 GPIO 0–29），GPIO 40–44 會出錯。
#ifndef _BOARDS_EXAM_RP2350B_H
#define _BOARDS_EXAM_RP2350B_H

pico_board_cmake_set(PICO_PLATFORM, rp2350)

#define PICO_RP2350A 0

// printf 走 UART0（板上標 "UART 0" 的 USB-C 很可能是 USB 轉 UART，接到 UART0）。
// TX/RX 腳位未知：看原理圖後改。預設 0/1。
#ifndef PICO_DEFAULT_UART
#define PICO_DEFAULT_UART 0
#endif
#ifndef PICO_DEFAULT_UART_TX_PIN
#define PICO_DEFAULT_UART_TX_PIN 0
#endif
#ifndef PICO_DEFAULT_UART_RX_PIN
#define PICO_DEFAULT_UART_RX_PIN 1
#endif

// 外部 flash 大小未知；寫小一點最安全（只影響 flash 存取 API 的上限）。
pico_board_cmake_set_default(PICO_FLASH_SIZE_BYTES, (2 * 1024 * 1024))
#ifndef PICO_FLASH_SIZE_BYTES
#define PICO_FLASH_SIZE_BYTES (2 * 1024 * 1024)
#endif

pico_board_cmake_set_default(PICO_RP2350_A2_SUPPORTED, 1)
#ifndef PICO_RP2350_A2_SUPPORTED
#define PICO_RP2350_A2_SUPPORTED 1
#endif

#endif
