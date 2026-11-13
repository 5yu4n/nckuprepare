// rv_tools.h：RP2350 Hazard3 專用指令的包裝 + 週期計數。Arm 或電腦上自動改用等效 C。
#pragma once
#include <stdint.h>

#if defined(__riscv)
// ---- 編譯器不會自己產生的 Zbb/Zbkb 指令 ----
static inline uint32_t rv_brev8(uint32_t x)  { uint32_t r; __asm__("brev8 %0, %1" : "=r"(r) : "r"(x)); return r; } // 每個 byte 內位元反轉
static inline uint32_t rv_orc_b(uint32_t x)  { uint32_t r; __asm__("orc.b %0, %1" : "=r"(r) : "r"(x)); return r; } // 非 0 的 byte → 0xFF
static inline uint32_t rv_zip(uint32_t x)    { uint32_t r; __asm__("zip %0, %1"   : "=r"(r) : "r"(x)); return r; } // 低半與高半位元交錯
static inline uint32_t rv_unzip(uint32_t x)  { uint32_t r; __asm__("unzip %0, %1" : "=r"(r) : "r"(x)); return r; } // zip 的反運算
// ---- 週期 / 指令計數（M-mode CSR；SDK 程式本來就在 M-mode）----
static inline void     rv_counters_on(void)  { __asm__ volatile("csrci mcountinhibit, 5"); } // 重置後預設是停的
static inline uint32_t rv_cycles(void)       { uint32_t r; __asm__ volatile("csrr %0, mcycle"   : "=r"(r)); return r; }
static inline uint32_t rv_instret(void)      { uint32_t r; __asm__ volatile("csrr %0, minstret" : "=r"(r)); return r; }
#else
static inline uint32_t rv_brev8(uint32_t x) {
    x = ((x >> 1) & 0x55555555u) | ((x & 0x55555555u) << 1);
    x = ((x >> 2) & 0x33333333u) | ((x & 0x33333333u) << 2);
    return ((x >> 4) & 0x0F0F0F0Fu) | ((x & 0x0F0F0F0Fu) << 4);
}
static inline uint32_t rv_orc_b(uint32_t x) {
    uint32_t r = 0;
    for (int i = 0; i < 32; i += 8) if ((x >> i) & 0xFFu) r |= 0xFFu << i;
    return r;
}
static inline uint32_t rv_zip(uint32_t x) {
    uint32_t r = 0;
    for (int i = 0; i < 16; i++) r |= ((x >> i) & 1u) << (2 * i) | ((x >> (i + 16)) & 1u) << (2 * i + 1);
    return r;
}
static inline uint32_t rv_unzip(uint32_t x) {
    uint32_t r = 0;
    for (int i = 0; i < 16; i++) r |= ((x >> (2 * i)) & 1u) << i | ((x >> (2 * i + 1)) & 1u) << (i + 16);
    return r;
}
static inline void     rv_counters_on(void) {}
static inline uint32_t rv_cycles(void)  { return 0; }   // Arm 上改用 time_us_64()
static inline uint32_t rv_instret(void) { return 0; }
#endif

static inline uint32_t rv_bitrev32(uint32_t x) { return __builtin_bswap32(rv_brev8(x)); }   // RISC-V：brev8 + rev8 兩條
// 第一個 0 byte 的位置（0–3），沒有就回傳 4：strlen / memchr 一次看 4 個 byte
static inline uint32_t rv_first_zero_byte(uint32_t w) {
    uint32_t m = ~rv_orc_b(w);
    return m ? (uint32_t)__builtin_ctz(m) >> 3 : 4;
}

// 用法：RV_MEASURE("名稱", 程式碼); 印出週期數與指令數（IPC 越接近 1 越好）
#define RV_MEASURE(name, code) do { \
    rv_counters_on(); \
    uint32_t c0_ = rv_cycles(), i0_ = rv_instret(); \
    code; \
    uint32_t c1_ = rv_cycles(), i1_ = rv_instret(); \
    printf("%s: %lu cycles, %lu instr\n", name, (unsigned long)(c1_ - c0_), (unsigned long)(i1_ - i0_)); \
} while (0)
