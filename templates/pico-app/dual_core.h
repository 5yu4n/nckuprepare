// dual_core.h：把一段可切割的工作交給兩顆核心同時做（pico-sdk）。
// 用法：
//   dual_init();                                   // main() 開頭呼叫一次（內部 multicore_launch_core1）
//   dual_run(work, ctx, n);                        // core0 做 [0, n/2)、core1 做 [n/2, n)，兩邊都做完才回來
//   void work(void *ctx, uint32_t lo, uint32_t hi, int core) { ... 只碰 [lo, hi) ... }
// 規則：
//   - 兩邊只寫自己那一段（或各自的 partial[core]），最後在 core0 合併，不用鎖。
//   - 工作太小（< 幾千個元素）時 FIFO 來回的成本會吃掉收益，直接單核做。
//   - 若想讓 core1 做較多（例如 core0 還要處理 USB / 中斷），用 dual_run_split 指定切點。
//   - 熱函式可加 __not_in_flash_func(work) 放 RAM，兩核同時從 flash 抓指令會互搶 XIP 快取。
#pragma once
#include <stdint.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"

typedef void (*dual_work_fn)(void *ctx, uint32_t lo, uint32_t hi, int core);
typedef struct { dual_work_fn fn; void *ctx; uint32_t lo, hi; } dual_job_t;
static dual_job_t dual_job1;

static void dual_core1_main(void) {
    while (true) {
        dual_job_t *j = (dual_job_t *)(uintptr_t)multicore_fifo_pop_blocking();   // FIFO 只傳指標
        j->fn(j->ctx, j->lo, j->hi, 1);
        multicore_fifo_push_blocking(1);                                         // 完成訊號
    }
}
static inline void dual_init(void) { multicore_launch_core1(dual_core1_main); }

static inline void dual_run_split(dual_work_fn fn, void *ctx, uint32_t n, uint32_t mid) {
    dual_job1.fn = fn; dual_job1.ctx = ctx; dual_job1.lo = mid; dual_job1.hi = n;
    multicore_fifo_push_blocking((uint32_t)(uintptr_t)&dual_job1);
    fn(ctx, 0, mid, 0);                       // core0 同時做前段
    (void)multicore_fifo_pop_blocking();      // 等 core1 做完
}
static inline void dual_run(dual_work_fn fn, void *ctx, uint32_t n) { dual_run_split(fn, ctx, n, n / 2); }
