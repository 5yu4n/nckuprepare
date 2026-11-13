#include <stdint.h>
#include <string.h>
//@@ radix
// LSD 基數排序 uint32：4 輪 × 8 位元，O(4n)，穩定。tmp 要 n 個空間。n 大於約 64 就比 qsort 快很多
// 有號 int32：排序前 x ^= 0x80000000u，排完再 XOR 回來
static void radix_sort_u32(uint32_t *a, uint32_t *tmp, uint32_t n) {
    uint32_t *src = a, *dst = tmp;
    for (int sh = 0; sh < 32; sh += 8) {
        uint32_t cnt[256] = {0}, sum = 0;
        for (uint32_t i = 0; i < n; i++) cnt[(src[i] >> sh) & 255]++;
        if (cnt[(src[0] >> sh) & 255] == n) continue;     // 這一輪全部同一桶：跳過
        for (int b = 0; b < 256; b++) { uint32_t c = cnt[b]; cnt[b] = sum; sum += c; }
        for (uint32_t i = 0; i < n; i++) dst[cnt[(src[i] >> sh) & 255]++] = src[i];
        uint32_t *t = src; src = dst; dst = t;
    }
    if (src != a) memcpy(a, src, n * sizeof *a);
}
// 依 key[] 排序「索引」idx[0..n-1]（例如依邊權排序邊），穩定
static void radix_sort_idx(uint32_t *idx, uint32_t *tmp, const uint32_t *key, uint32_t n) {
    uint32_t *src = idx, *dst = tmp;
    for (int sh = 0; sh < 32; sh += 8) {
        uint32_t cnt[256] = {0}, sum = 0;
        for (uint32_t i = 0; i < n; i++) cnt[(key[src[i]] >> sh) & 255]++;
        for (int b = 0; b < 256; b++) { uint32_t c = cnt[b]; cnt[b] = sum; sum += c; }
        for (uint32_t i = 0; i < n; i++) dst[cnt[(key[src[i]] >> sh) & 255]++] = src[i];
        uint32_t *t = src; src = dst; dst = t;
    }
    if (src != idx) memcpy(idx, src, n * sizeof *idx);
}
//@@ countsort
// 值域小（0..R-1）：計數排序 O(n + R)，不需要額外的 n 空間
static void counting_sort(uint32_t *a, uint32_t n, uint32_t *cnt, uint32_t R) {
    memset(cnt, 0, R * sizeof *cnt);
    for (uint32_t i = 0; i < n; i++) cnt[a[i]]++;
    for (uint32_t v = 0, k = 0; v < R; v++)
        for (uint32_t c = cnt[v]; c; c--) a[k++] = v;
}
// n <= 16 左右：插入排序最快（也拿來收尾）
static void insertion_sort(uint32_t *a, uint32_t n) {
    for (uint32_t i = 1; i < n; i++) {
        uint32_t x = a[i], j = i;
        while (j && a[j - 1] > x) { a[j] = a[j - 1]; j--; }
        a[j] = x;
    }
}
//@@ quickselect
// 第 k 小（0 起算），平均 O(n)，會打亂陣列。中位數、第 k 大、前 k 小都用它，不用整個排序
static uint32_t quickselect(uint32_t *a, uint32_t n, uint32_t k) {
    uint32_t lo = 0, hi = n - 1;
    while (hi > lo) {
        uint32_t mid = lo + (hi - lo) / 2, t;                  // 三數取中當 pivot
        if (a[mid] < a[lo]) { t = a[mid]; a[mid] = a[lo]; a[lo] = t; }
        if (a[hi] < a[lo]) { t = a[hi]; a[hi] = a[lo]; a[lo] = t; }
        if (a[hi] < a[mid]) { t = a[hi]; a[hi] = a[mid]; a[mid] = t; }
        uint32_t p = a[mid], i = lo, j = hi;
        while (i <= j) {                                        // Hoare 分割
            while (a[i] < p) i++;
            while (a[j] > p) j--;
            if (i <= j) { t = a[i]; a[i] = a[j]; a[j] = t; i++; if (j == 0) break; j--; }
        }
        if (k <= j) hi = j;
        else if (k >= i) lo = i;
        else return a[k];
    }
    return a[k];
}
