// par.h：Linux pthread 平行工具（一個標頭）。編譯加 -pthread。
//   par_for(n, work, ctx);                  把 [0, n) 切給 par_threads() 個執行緒，全部做完才回來
//   void work(void *ctx, long lo, long hi, int tid) { ... 只碰 [lo, hi)，結果寫自己的 slot[tid] ... }
//   par_slot_t slot[PAR_MAXT];              每執行緒一條快取行的結果槽（避免偽共享），最後在主執行緒合併
// 執行緒數預設 = 線上核心數；環境變數 PAR_THREADS 可覆蓋（OJ 機器核心數未知時用來試）。
// 規則：各算各的、用區域變數累加、最後寫一次 slot；不要每個元素都上鎖或 atomic。
#pragma once
#define _GNU_SOURCE
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>

#define PAR_MAXT 64
typedef struct { _Alignas(64) int64_t i64; double f64; uint64_t u64; } par_slot_t;
typedef void (*par_work_fn)(void *ctx, long lo, long hi, int tid);

static inline int par_threads(void) {
    const char *e = getenv("PAR_THREADS");
    long n = e ? atol(e) : sysconf(_SC_NPROCESSORS_ONLN);
    if (n < 1) n = 1;
    if (n > PAR_MAXT) n = PAR_MAXT;
    return (int)n;
}

typedef struct { par_work_fn fn; void *ctx; long lo, hi; int tid; } par_arg_t;
static void *par_tramp(void *p) { par_arg_t *a = (par_arg_t *)p; a->fn(a->ctx, a->lo, a->hi, a->tid); return NULL; }

// 主執行緒自己做第 0 段，少開一條執行緒；n 太小時直接單執行緒
static inline void par_for_n(long n, par_work_fn fn, void *ctx, int nt) {
    if (nt <= 1 || n < 10000) { fn(ctx, 0, n, 0); return; }
    pthread_t th[PAR_MAXT]; par_arg_t arg[PAR_MAXT];
    for (int t = 0; t < nt; t++) {
        arg[t].fn = fn; arg[t].ctx = ctx; arg[t].tid = t;
        arg[t].lo = (long)((__int128)n * t / nt); arg[t].hi = (long)((__int128)n * (t + 1) / nt);
    }
    for (int t = 1; t < nt; t++) pthread_create(&th[t], NULL, par_tramp, &arg[t]);
    fn(ctx, arg[0].lo, arg[0].hi, 0);
    for (int t = 1; t < nt; t++) pthread_join(th[t], NULL);
}
static inline void par_for(long n, par_work_fn fn, void *ctx) { par_for_n(n, fn, ctx, par_threads()); }
