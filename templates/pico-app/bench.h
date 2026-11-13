// bench.h：計時 + 對拍。板子（pico-sdk）和 Linux 都能用。
#pragma once
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
  #include "pico/stdlib.h"
  static inline uint64_t bench_now_us(void) { return time_us_64(); }
  #if defined(__riscv)
    static inline void     bench_cycles_on(void) { __asm__ volatile("csrci mcountinhibit, 5"); }
    static inline uint32_t bench_cycles(void)    { uint32_t r; __asm__ volatile("csrr %0, mcycle" : "=r"(r)); return r; }
  #else
    static inline void     bench_cycles_on(void) {}
    static inline uint32_t bench_cycles(void)    { return 0; }   // Arm：只看微秒
  #endif
#else
  #include <time.h>
  static inline uint64_t bench_now_us(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (uint64_t)t.tv_sec * 1000000u + (uint64_t)t.tv_nsec / 1000u; }
  static inline void     bench_cycles_on(void) {}
  static inline uint32_t bench_cycles(void)    { return 0; }
#endif

// 跑 reps 次取最快：BENCH("名稱", 5, 程式碼);
#define BENCH(name, reps, code) do { \
    uint64_t best_us_ = UINT64_MAX; uint32_t best_cy_ = UINT32_MAX; \
    bench_cycles_on(); \
    for (int r_ = 0; r_ < (reps); r_++) { \
        uint32_t c0_ = bench_cycles(); uint64_t t0_ = bench_now_us(); \
        code; \
        uint64_t dt_ = bench_now_us() - t0_; uint32_t dc_ = bench_cycles() - c0_; \
        if (dt_ < best_us_) best_us_ = dt_; \
        if (dc_ < best_cy_) best_cy_ = dc_; \
    } \
    printf("%-24s %10llu us %12lu cycles\n", name, (unsigned long long)best_us_, (unsigned long)best_cy_); \
} while (0)

// 比對兩塊記憶體：相同回傳 1；不同印出第一個不同的位置（以 uint32 為單位）
static inline int bench_same_u32(const char *name, const uint32_t *a, const uint32_t *b, size_t n) {
    for (size_t i = 0; i < n; i++)
        if (a[i] != b[i]) {
            printf("%s: MISMATCH at %u: ref=0x%08lx fast=0x%08lx\n", name, (unsigned)i, (unsigned long)a[i], (unsigned long)b[i]);
            return 0;
        }
    printf("%s: ok (%u words)\n", name, (unsigned)n);
    return 1;
}

// 固定種子的亂數（每次跑結果一樣，不要用 rand()）
static uint32_t bench_rng = 2463534242u;
static inline uint32_t bench_rand(void) { bench_rng ^= bench_rng << 13; bench_rng ^= bench_rng >> 17; bench_rng ^= bench_rng << 5; return bench_rng; }
