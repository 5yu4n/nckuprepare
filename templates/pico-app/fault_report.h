// fault_report.h：當機時印出原因（RISC-V 預設會讓核心靜靜睡著）。main() 開頭呼叫 fault_report_install()。
// 印出的 pc 拿去 build/app.dis 搜尋，就知道是哪一行。CMake 要連 hardware_exception。
#pragma once
#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/exception.h"

#if defined(__riscv)
static void fault_handler(void) {
    uint32_t cause, pc;
    __asm__ volatile("csrr %0, mcause" : "=r"(cause));
    __asm__ volatile("csrr %0, mepc"   : "=r"(pc));
    // 4/6 = 讀/寫沒對齊，5/7 = 讀/寫到不存在的位址，2 = 非法指令
    // Hazard3 的 mtval 固定是 0，拿不到出錯的位址；用 pc 對 .dis 檔找是哪一行
    printf("\nFAULT mcause=%lu pc=0x%08lx\n", (unsigned long)cause, (unsigned long)pc);
    while (1) tight_loop_contents();
}
static void fault_report_install(void) {
    exception_set_exclusive_handler(LOAD_ALIGN_EXCEPTION,  fault_handler);
    exception_set_exclusive_handler(STORE_ALIGN_EXCEPTION, fault_handler);
    exception_set_exclusive_handler(LOAD_FAULT_EXCEPTION,  fault_handler);
    exception_set_exclusive_handler(STORE_FAULT_EXCEPTION, fault_handler);
    exception_set_exclusive_handler(INSTR_ILLEGAL_EXCEPTION, fault_handler);
}
#else
static void fault_handler(void) {
    printf("\nHARDFAULT\n");
    while (1) tight_loop_contents();
}
static void fault_report_install(void) { exception_set_exclusive_handler(HARDFAULT_EXCEPTION, fault_handler); }
#endif
