// main.c：考場用的「原版 vs 新版」對拍 + 計時骨架。板子（pico-sdk）和電腦（gcc）都能編。
//
// 照順序改這 4 個地方（搜尋「改這裡」）：
//   1. work_ref   ：貼題目原本的函式，一個字都不要改，它就是正確答案。
//   2. work_fast  ：你的優化版（或叫小模型寫的版本）。簽名和 work_ref 一樣。
//   3. work_dual  ：（選用）兩顆核心版，用 dual_core.h。電腦上會自動跳過。
//   4. make_input ：照題目的輸入範圍產生測資。
//
// 電腦上先驗證邏輯：gcc -O2 -Wall main.c -o t && ./t
// 板子上：照 CMakeLists.txt 建置、燒錄，看序列埠輸出。
// 輸出最後一行是 ALL PASS 才可以交；FAIL 會印第一個不同的位置。
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "bench.h"

#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
  #include "pico/stdlib.h"
  #include "fault_report.h"
  #include "dual_core.h"
  #define HAVE_DUAL 1
#else
  #define HAVE_DUAL 0
  #define __not_in_flash_func(f) f
#endif

#define N 16384                       // 改這裡：最大輸入長度。RP2350 SRAM 520 KB：三個 uint32 陣列 × N × 4 bytes 要放得下（N=32768 約 384 KB 是上限附近）
static uint32_t in[N], out_ref[N], out_fast[N];

// ===== 1. 原版（改這裡：貼題目給的函式）=====
// 範例：每個元素算「1 的個數 × 3 + 最高位元位置」，用逐位元迴圈（慢）
static void work_ref(const uint32_t *a, uint32_t *o, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) {
        uint32_t c = 0, hi = 0;
        for (int b = 0; b < 32; b++) if ((a[i] >> b) & 1) { c++; hi = (uint32_t)b; }
        o[i] = c * 3 + hi;
    }
}

// ===== 2. 優化版（改這裡）=====
// 範例：popcount → cpop 一條指令；最高位元 = 31 - clz（輸入 0 要特判，clz(0) 未定義）
static void __not_in_flash_func(work_fast)(const uint32_t *a, uint32_t *o, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) {
        uint32_t x = a[i];
        o[i] = x ? (uint32_t)__builtin_popcount(x) * 3 + (31u - (uint32_t)__builtin_clz(x)) : 0;
    }
}

// ===== 3. 兩顆核心版（選用，改這裡）=====
#if HAVE_DUAL
typedef struct { const uint32_t *a; uint32_t *o; } work_ctx_t;
static void __not_in_flash_func(work_part)(void *ctx, uint32_t lo, uint32_t hi, int core) {
    (void)core;
    work_ctx_t *c = (work_ctx_t *)ctx;
    work_fast(c->a + lo, c->o + lo, hi - lo);      // 每核做自己那段，互不干擾
}
static void work_dual(const uint32_t *a, uint32_t *o, uint32_t n) {
    if (n < 4096) { work_fast(a, o, n); return; }  // 太小不值得叫 core1
    work_ctx_t c = { a, o };
    dual_run(work_part, &c, n);
}
#endif

// ===== 4. 測資（改這裡）=====
// kind 0 亂數、1 全 0、2 全 1 位元、3 遞增、4 遞減、5 只有最高位元、6 小數值
#define KINDS 7
static void make_input(uint32_t *a, uint32_t n, int kind) {
    for (uint32_t i = 0; i < n; i++) {
        switch (kind) {
            case 0: a[i] = bench_rand(); break;
            case 1: a[i] = 0; break;
            case 2: a[i] = 0xFFFFFFFFu; break;
            case 3: a[i] = i; break;
            case 4: a[i] = n - i; break;
            case 5: a[i] = 0x80000000u >> (i & 31); break;
            default: a[i] = bench_rand() & 0xFF; break;
        }
    }
}
// 長度：0、1、不是 4 的倍數、剛好跨過 dual 門檻、最大值
static const uint32_t sizes[] = { 0, 1, 2, 3, 4, 5, 7, 8, 31, 33, 255, 1000, 4095, 4096, 4097, N };

typedef void (*work_fn)(const uint32_t *, uint32_t *, uint32_t);
static int check(const char *name, work_fn f) {
    for (int k = 0; k < KINDS; k++)
        for (unsigned s = 0; s < sizeof sizes / sizeof sizes[0]; s++) {
            uint32_t n = sizes[s];
            make_input(in, n, k);
            memset(out_ref, 0xA5, sizeof out_ref);     // 填垃圾：抓「沒寫到」的元素
            memset(out_fast, 0x5A, sizeof out_fast);
            work_ref(in, out_ref, n);
            f(in, out_fast, n);
            if (n && memcmp(out_ref, out_fast, n * sizeof out_ref[0])) {
                printf("FAIL %s kind=%d n=%u\n", name, k, (unsigned)n);
                bench_same_u32(name, out_ref, out_fast, n);
                return 0;
            }
        }
    printf("%s: ok (%d kinds x %u sizes)\n", name, KINDS, (unsigned)(sizeof sizes / sizeof sizes[0]));
    return 1;
}

int main(void) {
#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
    stdio_init_all();
    fault_report_install();
#if LIB_PICO_STDIO_USB
    for (int i = 0; i < 300 && !stdio_usb_connected(); i++) sleep_ms(10);   // 等 USB 序列埠最多 3 秒
#else
    sleep_ms(100);
#endif
    dual_init();
#endif
    int ok = check("work_fast", work_fast);
#if HAVE_DUAL
    ok = ok && check("work_dual", work_dual);
#endif
    if (!ok) { printf("FAIL\n"); return 1; }

    make_input(in, N, 0);
    BENCH("work_ref", 3, work_ref(in, out_ref, N));
    BENCH("work_fast", 5, work_fast(in, out_fast, N));
#if HAVE_DUAL
    BENCH("work_dual", 5, work_dual(in, out_fast, N));
#endif
    printf("check %08lx\n", (unsigned long)out_fast[N / 2]);   // 用掉結果，避免 -O3 把整段刪掉
    printf("ALL PASS\n");
#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
    while (true) sleep_ms(1000);    // 不要 return：板子上 main 回傳後就停了，USB 輸出可能還沒送完
#endif
    return 0;
}
