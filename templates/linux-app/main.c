// main.c（Linux）：原版 vs 新版 對拍 + 計時 + pthread 平行版。
// 編譯：make            （= gcc -O3 -Wall -pthread main.c -o app）
// 執行：./app           （PAR_THREADS=2 ./app 可以指定執行緒數）
// 照順序改「改這裡」的 4 個地方：work_ref、work_fast、work_part（平行版的一段）、make_input。
// 最後一行是 ALL PASS 才可以交。
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "../pico-app/bench.h"
#include "par.h"

#define N (1 << 22)                   // 改這裡：最大輸入長度
static int32_t in[N];

// ===== 1. 原版（改這裡：貼題目給的函式）=====
// 範例：統計「a[i] 是 3 的倍數且是正數」的元素和（用 % 運算，慢）
static int64_t work_ref(const int32_t *a, long n) {
    int64_t s = 0;
    for (long i = 0; i < n; i++) if (a[i] > 0 && a[i] % 3 == 0) s += a[i];
    return s;
}
// ===== 2. 優化版（改這裡）=====
// 範例：無分支寫法讓編譯器向量化；% 3 是常數，編譯器會轉成乘法
static int64_t work_fast(const int32_t *a, long n) {
    int64_t s0 = 0, s1 = 0;
    long i = 0;
    for (; i + 2 <= n; i += 2) {
        int32_t x = a[i], y = a[i + 1];
        s0 += (x > 0 && x % 3 == 0) ? x : 0;
        s1 += (y > 0 && y % 3 == 0) ? y : 0;
    }
    if (i < n) s0 += (a[i] > 0 && a[i] % 3 == 0) ? a[i] : 0;
    return s0 + s1;
}
// ===== 3. 平行版：每個執行緒做一段（改這裡）=====
typedef struct { const int32_t *a; par_slot_t slot[PAR_MAXT]; } ctx_t;
static void work_part(void *p, long lo, long hi, int tid) {
    ctx_t *c = (ctx_t *)p;
    c->slot[tid].i64 = work_fast(c->a + lo, hi - lo);   // 各寫各的 slot，最後合併
}
static ctx_t ctx;
static int64_t work_par(const int32_t *a, long n) {
    ctx.a = a;
    int nt = par_threads();
    for (int t = 0; t < nt; t++) ctx.slot[t].i64 = 0;
    par_for_n(n, work_part, &ctx, nt);
    int64_t s = 0;
    for (int t = 0; t < nt; t++) s += ctx.slot[t].i64;
    return s;
}
// ===== 4. 測資（改這裡）=====
#define KINDS 6
static void make_input(int32_t *a, long n, int kind) {
    for (long i = 0; i < n; i++) {
        switch (kind) {
            case 0: a[i] = (int32_t)bench_rand(); break;
            case 1: a[i] = 0; break;
            case 2: a[i] = INT32_MAX; break;
            case 3: a[i] = INT32_MIN; break;
            case 4: a[i] = (int32_t)i - (int32_t)(n / 2); break;
            default: a[i] = (int32_t)(bench_rand() % 1000); break;
        }
    }
}
static const long sizes[] = { 0, 1, 2, 3, 7, 9999, 10000, 10001, 65537, N };

typedef int64_t (*fn_t)(const int32_t *, long);
static int check(const char *name, fn_t f) {
    for (int k = 0; k < KINDS; k++)
        for (unsigned s = 0; s < sizeof sizes / sizeof sizes[0]; s++) {
            make_input(in, sizes[s], k);
            int64_t r = work_ref(in, sizes[s]), g = f(in, sizes[s]);
            if (r != g) { printf("FAIL %s kind=%d n=%ld ref=%lld got=%lld\n", name, k, sizes[s], (long long)r, (long long)g); return 0; }
        }
    printf("%s: ok\n", name);
    return 1;
}
int main(void) {
    if (!check("work_fast", work_fast) || !check("work_par", work_par)) { puts("FAIL"); return 1; }
    make_input(in, N, 0);
    volatile int64_t sink;
    BENCH("work_ref", 3, sink = work_ref(in, N));
    BENCH("work_fast", 5, sink = work_fast(in, N));
    BENCH("work_par", 5, sink = work_par(in, N));
    (void)sink;
    printf("threads=%d\nALL PASS\n", par_threads());
    return 0;
}
