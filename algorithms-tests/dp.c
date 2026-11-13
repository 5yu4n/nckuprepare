#include <stdint.h>
#include <string.h>
//@@ prefix
// 前綴和：P[i] = a[0]+..+a[i-1]，區間和 sum(l..r) = P[r+1]-P[l]，每次查詢 O(1)。用 int64 防溢位
static void prefix_build(const int32_t *a, uint32_t n, int64_t *P) {
    P[0] = 0;
    for (uint32_t i = 0; i < n; i++) P[i + 1] = P[i] + a[i];
}
// 2D：P 大小 (H+1)*(W+1)；矩形 (r1,c1)~(r2,c2) 含端點 = 四個角相加減
static void prefix2d_build(const int32_t *a, uint32_t H, uint32_t W, int64_t *P) {
    memset(P, 0, (W + 1) * sizeof *P);
    for (uint32_t r = 0; r < H; r++) {
        int64_t row = 0;
        P[(r + 1) * (W + 1)] = 0;
        for (uint32_t c = 0; c < W; c++) {
            row += a[r * W + c];
            P[(r + 1) * (W + 1) + c + 1] = P[r * (W + 1) + c + 1] + row;
        }
    }
}
static int64_t rect_sum(const int64_t *P, uint32_t W, uint32_t r1, uint32_t c1, uint32_t r2, uint32_t c2) {
    uint32_t w = W + 1;
    return P[(r2 + 1) * w + c2 + 1] - P[r1 * w + c2 + 1] - P[(r2 + 1) * w + c1] + P[r1 * w + c1];
}
//@@ diff
// 差分陣列：很多次「區間 [l,r] 全部加 v」最後才要結果 → 每次 O(1)，最後一次前綴和 O(n)
// D 大小 n+1，先清零
static inline void diff_add(int64_t *D, uint32_t l, uint32_t r, int64_t v) { D[l] += v; D[r + 1] -= v; }
static void diff_apply(const int64_t *D, uint32_t n, int64_t *a) {     // a[i] += 累積
    int64_t run = 0;
    for (uint32_t i = 0; i < n; i++) { run += D[i]; a[i] += run; }
}
// 2D 差分：矩形加 v → 四個角，最後做 2D 前綴和。D 大小 (H+1)*(W+1)
static inline void diff2d_add(int64_t *D, uint32_t W, uint32_t r1, uint32_t c1, uint32_t r2, uint32_t c2, int64_t v) {
    uint32_t w = W + 1;
    D[r1 * w + c1] += v; D[r1 * w + c2 + 1] -= v; D[(r2 + 1) * w + c1] -= v; D[(r2 + 1) * w + c2 + 1] += v;
}
static void diff2d_apply(int64_t *D, uint32_t H, uint32_t W) {         // 原地變成每格的增量
    uint32_t w = W + 1;
    for (uint32_t r = 0; r < H; r++)
        for (uint32_t c = 0; c < W; c++)
            D[r * w + c] += (c ? D[r * w + c - 1] : 0) + (r ? D[(r - 1) * w + c] : 0) - (r && c ? D[(r - 1) * w + c - 1] : 0);
}
//@@ fenwick
// Fenwick（BIT）：單點加值 + 前綴和，兩個都 O(log n)，陣列 n+1 個，索引從 1 開始
// 「邊改邊問」的區間和、逆序數對、第 k 小（值域 BIT）都用它
static void bit_add(int64_t *T, uint32_t n, uint32_t i, int64_t v) { for (; i <= n; i += i & (0u - i)) T[i] += v; }
static int64_t bit_sum(const int64_t *T, uint32_t i) { int64_t s = 0; for (; i; i -= i & (0u - i)) s += T[i]; return s; }  // a[1..i]
// 值域 BIT 上找「前綴和 >= k 的最小位置」（第 k 小），O(log n)，元素非負
static uint32_t bit_lower_bound(const int64_t *T, uint32_t n, int64_t k) {
    uint32_t pos = 0;
    for (uint32_t step = 1u << (31 - __builtin_clz(n)); step; step >>= 1)
        if (pos + step <= n && T[pos + step] < k) { pos += step; k -= T[pos]; }
    return pos + 1;
}
// 區間加 + 區間和：兩棵 BIT。區間 [l,r] 加 v；前綴和 sum(1..i) = B1(i)*i - B2(i)
static void rbit_add(int64_t *B1, int64_t *B2, uint32_t n, uint32_t l, uint32_t r, int64_t v) {
    bit_add(B1, n, l, v); bit_add(B1, n, r + 1, -v);
    bit_add(B2, n, l, v * (int64_t)(l - 1)); bit_add(B2, n, r + 1, -v * (int64_t)r);
}
static int64_t rbit_prefix(const int64_t *B1, const int64_t *B2, uint32_t i) { return bit_sum(B1, i) * (int64_t)i - bit_sum(B2, i); }
// 逆序數對 O(n log n)：值先壓縮到 1..n，由右往左，答案 += bit_sum(v-1)，再 bit_add(v,1)
//@@ segtree
// 線段樹（非遞迴、由下往上）：單點設值 + 區間查詢，任何可結合運算（min/max/sum/gcd）都行
// 陣列 2n 個。查詢 [l, r) 左閉右開
#define SEGN 65536
static int32_t sg[2 * SEGN];
static uint32_t sgn;
#define SEG_OP(a, b) ((a) < (b) ? (a) : (b))     // 換成 + 就是區間和，max 就是區間最大
#define SEG_ID INT32_MAX                         // OP 的單位元（min → 最大值，sum → 0）
static void seg_build(const int32_t *a, uint32_t n) {
    sgn = n;
    for (uint32_t i = 0; i < n; i++) sg[n + i] = a[i];
    for (uint32_t i = n - 1; i > 0; i--) sg[i] = SEG_OP(sg[2 * i], sg[2 * i + 1]);
}
static void seg_set(uint32_t i, int32_t v) {
    for (sg[i += sgn] = v; i > 1; i >>= 1) sg[i >> 1] = SEG_OP(sg[i], sg[i ^ 1]);
}
static int32_t seg_query(uint32_t l, uint32_t r) {
    int32_t res = SEG_ID;
    for (l += sgn, r += sgn; l < r; l >>= 1, r >>= 1) {
        if (l & 1) { res = SEG_OP(res, sg[l]); l++; }
        if (r & 1) { --r; res = SEG_OP(res, sg[r]); }
    }
    return res;
}
//@@ sparse
// Sparse table：陣列不再改變時的區間最小值，建表 O(n log n)、查詢 O(1)
// RAM = 4 * n * LOG bytes（n=16384, LOG=15 → 960 KB 太大！n=4096 → 208 KB）。會改值就用線段樹
#define STN 4096
#define STLOG 13
static int32_t st[STLOG][STN];
static void sparse_build(const int32_t *a, uint32_t n) {
    memcpy(st[0], a, n * sizeof *a);
    for (uint32_t j = 1; (1u << j) <= n; j++)
        for (uint32_t i = 0; i + (1u << j) <= n; i++) {
            int32_t x = st[j - 1][i], y = st[j - 1][i + (1u << (j - 1))];
            st[j][i] = x < y ? x : y;
        }
}
static int32_t sparse_min(uint32_t l, uint32_t r) {           // [l, r] 含端點
    uint32_t j = 31 - (uint32_t)__builtin_clz(r - l + 1);
    int32_t x = st[j][l], y = st[j][r + 1 - (1u << j)];
    return x < y ? x : y;
}
//@@ window
// 滑動視窗最大值：單調遞減雙端佇列，O(n) 取代 O(nk)。out[i] = max(a[i..i+k-1])
static void window_max(const int32_t *a, uint32_t n, uint32_t k, int32_t *out, uint32_t *dqbuf) {
    uint32_t h = 0, t = 0;                         // dqbuf 存索引，大小 n
    for (uint32_t i = 0; i < n; i++) {
        while (t > h && a[dqbuf[t - 1]] <= a[i]) t--;   // 後面比較大，前面的永遠不會是答案
        dqbuf[t++] = i;
        if (dqbuf[h] + k <= i) h++;                     // 掉出視窗
        if (i + 1 >= k) out[i + 1 - k] = a[dqbuf[h]];
    }
}
// 下一個更大元素：單調堆疊 O(n)。nxt[i] = 右邊第一個 > a[i] 的索引，沒有就是 n
// 「每根柱子能延伸多遠」「直方圖最大矩形」「股票跨度」都是這招
static void next_greater(const int32_t *a, uint32_t n, uint32_t *nxt, uint32_t *stkbuf) {
    uint32_t sp = 0;
    for (uint32_t i = 0; i < n; i++) {
        while (sp && a[stkbuf[sp - 1]] < a[i]) nxt[stkbuf[--sp]] = i;
        stkbuf[sp++] = i;
    }
    while (sp) nxt[stkbuf[--sp]] = n;
}
//@@ twoptr
// 雙指標：非負陣列中「和 <= S 的最長連續子陣列」，O(n) 取代 O(n^2)
static uint32_t longest_sum_le(const uint32_t *a, uint32_t n, uint64_t S) {
    uint64_t sum = 0;
    uint32_t best = 0, l = 0;
    for (uint32_t r = 0; r < n; r++) {
        sum += a[r];
        while (sum > S) sum -= a[l++];
        if (r + 1 - l > best) best = r + 1 - l;
    }
    return best;
}
// 已排序陣列中，和為 target 的數對個數（兩端往中間夾），O(n)
static uint64_t count_pairs_sum(const int32_t *a, uint32_t n, int64_t target) {
    uint64_t cnt = 0;
    uint32_t i = 0, j = n ? n - 1 : 0;
    while (n && i < j) {
        int64_t s = (int64_t)a[i] + a[j];
        if (s < target) i++;
        else if (s > target) j--;
        else if (a[i] == a[j]) { uint64_t k = j - i + 1; cnt += k * (k - 1) / 2; break; }
        else {
            uint32_t ci = 1, cj = 1;
            while (i + ci < j && a[i + ci] == a[i]) ci++;
            while (j - cj > i + ci - 1 && a[j - cj] == a[j]) cj++;
            cnt += (uint64_t)ci * cj; i += ci; j -= cj;
        }
    }
    return cnt;
}
// Kadane：最大連續子陣列和 O(n)
static int64_t max_subarray(const int32_t *a, uint32_t n) {
    int64_t best = a[0], cur = 0;
    for (uint32_t i = 0; i < n; i++) {
        cur = (cur > 0 ? cur : 0) + a[i];
        if (cur > best) best = cur;
    }
    return best;
}
//@@ bsearch
// lower_bound：第一個 >= x 的位置（沒有就回傳 n）。無分支寫法，迴圈次數固定 log n
static uint32_t lower_bound_u32(const uint32_t *a, uint32_t n, uint32_t x) {
    const uint32_t *base = a;
    while (n > 1) {
        uint32_t half = n / 2;
        base = (base[half - 1] < x) ? base + half : base;
        n -= half;
    }
    return (uint32_t)(base - a) + (n == 1 && *base < x);
}
// 對答案二分：ok(x) 對 x 單調（false…false true…true），找最小的 true。O(log(hi-lo) × 檢查成本)
// 例：「最小的最大值」「最少天數」「能切成 k 段的最大長度」
static uint32_t bs_k; static const uint32_t *bs_a; static uint32_t bs_n;
static int ok_split(uint64_t cap) {               // 範例：陣列能不能切成 <= bs_k 段且每段和 <= cap
    uint64_t run = 0; uint32_t parts = 1;
    for (uint32_t i = 0; i < bs_n; i++) {
        if (bs_a[i] > cap) return 0;
        if (run + bs_a[i] > cap) { parts++; run = 0; }
        run += bs_a[i];
    }
    return parts <= bs_k;
}
static uint64_t bs_answer(uint64_t lo, uint64_t hi) {   // 保證 ok(hi) 為真
    while (lo < hi) {
        uint64_t mid = lo + (hi - lo) / 2;
        if (ok_split(mid)) hi = mid; else lo = mid + 1;
    }
    return lo;
}
//@@ lis
// 最長嚴格遞增子序列 O(n log n)：tail[k] = 長度 k+1 的遞增子序列最小結尾
// 「不嚴格遞增」把 lower_bound 改成找第一個 > x（upper_bound）
static uint32_t lis_len(const uint32_t *a, uint32_t n, uint32_t *tail) {
    uint32_t len = 0;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t p = lower_bound_u32(tail, len, a[i]);
        tail[p] = a[i];
        if (p == len) len++;
    }
    return len;
}
//@@ knapsack
// 0/1 背包：一維陣列、容量「由大往小」掃，O(nC) 時間、O(C) 記憶體
static uint32_t knap01(const uint32_t *w, const uint32_t *v, uint32_t n, uint32_t C, uint32_t *dp) {
    memset(dp, 0, (C + 1) * sizeof *dp);
    for (uint32_t i = 0; i < n; i++)
        for (uint32_t c = C; c >= w[i] && c != UINT32_MAX; c--)
            if (dp[c - w[i]] + v[i] > dp[c]) dp[c] = dp[c - w[i]] + v[i];
    return dp[C];
}
// 無限背包（每樣可拿無限次）：同樣一維，但容量「由小往大」掃
static uint32_t knap_unbounded(const uint32_t *w, const uint32_t *v, uint32_t n, uint32_t C, uint32_t *dp) {
    memset(dp, 0, (C + 1) * sizeof *dp);
    for (uint32_t i = 0; i < n; i++)
        for (uint32_t c = w[i]; c <= C; c++)
            if (dp[c - w[i]] + v[i] > dp[c]) dp[c] = dp[c - w[i]] + v[i];
    return dp[C];
}
// 子集和可行性（只問「湊不湊得到」）：位元集合 bits |= bits << w，一次 32 個容量，O(nC/32)
static void subset_sum_bits(const uint32_t *w, uint32_t n, uint32_t C, uint32_t *bits) {
    uint32_t words = C / 32 + 1;
    memset(bits, 0, words * sizeof *bits);
    bits[0] = 1;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t ws = w[i] >> 5, bs = w[i] & 31;
        for (uint32_t j = words; j-- > ws;) {        // 由高往低，像 0/1 背包
            uint32_t lo = bits[j - ws] << bs;
            if (bs && j - ws > 0) lo |= bits[j - ws - 1] >> (32 - bs);
            bits[j] |= lo;
        }
    }
    if ((C + 1) & 31) bits[words - 1] &= (1u << ((C + 1) & 31)) - 1;   // 清掉超過 C 的位元
}
//@@ bitmask
// 位元 DP：旅行推銷員（TSP）O(2^n n^2)，取代 O(n!)。n=12 → 2^12*12*4 = 196 KB；n=16 要 4 MB 放不下
#define TSPN 12
static uint32_t tsp_dp[1u << TSPN][TSPN];
static uint32_t tsp(uint32_t n, uint32_t d[TSPN][TSPN]) {         // 從 0 出發、走完回到 0 的最短
    uint32_t full = (1u << n) - 1, best = 0xFFFFFFFFu;
    for (uint32_t m = 0; m <= full; m++) for (uint32_t i = 0; i < n; i++) tsp_dp[m][i] = 0xFFFFFFFFu;
    tsp_dp[1][0] = 0;
    for (uint32_t m = 1; m <= full; m += 2)                       // 只看含 0 的集合
        for (uint32_t i = 0; i < n; i++) {
            uint32_t cur = tsp_dp[m][i];
            if (cur == 0xFFFFFFFFu) continue;
            for (uint32_t rest = full & ~m; rest; rest &= rest - 1) {
                uint32_t j = (uint32_t)__builtin_ctz(rest), nm = m | (1u << j);
                if (cur + d[i][j] < tsp_dp[nm][j]) tsp_dp[nm][j] = cur + d[i][j];
            }
        }
    for (uint32_t i = 0; i < n; i++)
        if (tsp_dp[full][i] != 0xFFFFFFFFu && tsp_dp[full][i] + d[i][0] < best) best = tsp_dp[full][i] + d[i][0];
    return n == 1 ? 0 : best;
}
// SOS DP（子集和）：F[m] = Σ_{s ⊆ m} f[s]，O(n 2^n) 取代 O(3^n)。原地做
static void sos(uint32_t *f, uint32_t n) {
    for (uint32_t b = 0; b < n; b++)
        for (uint32_t m = 0; m < (1u << n); m++)
            if (m >> b & 1) f[m] += f[m ^ (1u << b)];
}
//@@ mitm
// 折半搜尋：n <= 40 的子集和計數（和 <= T 的子集數），O(2^(n/2)) 取代 O(2^n)
// 每半邊「合併式」產生已排序子集和（不用排序）。RAM = 3 × 4 × 2^(n/2) bytes：n=30 → 384 KB
#define MH 15
static uint32_t ma[1u << MH], mb[1u << MH], mtmp[1u << MH];
static uint32_t gen_sorted_sums(const uint32_t *w, uint32_t k, uint32_t *out) {
    uint32_t len = 1;
    out[0] = 0;
    for (uint32_t i = 0; i < k; i++) {                       // 合併 out 與 out + w[i]
        uint32_t a = 0, b = 0, t = 0;
        while (a < len && b < len)
            mtmp[t++] = (out[a] <= out[b] + w[i]) ? out[a++] : out[b++] + w[i];
        while (a < len) mtmp[t++] = out[a++];
        while (b < len) mtmp[t++] = out[b++] + w[i];
        len = t;
        memcpy(out, mtmp, len * sizeof *out);
    }
    return len;
}
static uint64_t mitm_count_le(const uint32_t *w, uint32_t n, uint32_t T) {   // 子集和要 < 2^32
    uint32_t h = n / 2, la = gen_sorted_sums(w, h, ma), lb = gen_sorted_sums(w + h, n - h, mb);
    uint64_t cnt = 0;
    uint32_t j = lb;
    for (uint32_t i = 0; i < la; i++) {                       // a 由小到大，b 的指標由大往小
        while (j > 0 && (uint64_t)ma[i] + mb[j - 1] > T) j--;
        cnt += j;
    }
    return cnt;
}
//@@ compress
// 座標壓縮：把大範圍的值換成 0..k-1（排序 + 去重 + 二分）。之後就能開陣列、BIT
static uint32_t compress(uint32_t *vals, uint32_t n, uint32_t *tmp) {     // vals 會變成去重後的排序值
    radix_sort_u32(vals, tmp, n);
    uint32_t k = 0;
    for (uint32_t i = 0; i < n; i++) if (!k || vals[i] != vals[k - 1]) vals[k++] = vals[i];
    return k;                                                 // 值 x 的編號 = lower_bound_u32(vals, k, x)
}
