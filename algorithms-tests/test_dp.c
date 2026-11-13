#define _POSIX_C_SOURCE 199309L
#include "nt.c"
#include "sort.c"
#include "dp.c"
#include "str.c"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
static int fails = 0;
#define CHECK(c) do { if (!(c)) { fails++; if (fails < 20) printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }
static uint64_t rs = 88172645463325252ull;
static uint64_t rnd(void) { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return rs; }
#define BIG 200000
static int32_t A[BIG], O1[BIG], O2[BIG];
static uint32_t U[BIG], U2[BIG], T1[BIG], T2[BIG];
static int64_t P[BIG + 1], D[BIG + 2], E[BIG + 2], F[BIG + 2];
static volatile int64_t sink;
int main(void) {
    double t0, t1, t2;
    // prefix + diff
    for (int tc = 0; tc < 200; tc++) {
        uint32_t n = (uint32_t)(rnd() % 500) + 1;
        for (uint32_t i = 0; i < n; i++) A[i] = (int32_t)(rnd() % 2000001) - 1000000;
        prefix_build(A, n, P);
        for (int q = 0; q < 200; q++) { uint32_t l = (uint32_t)(rnd() % n), r = l + (uint32_t)(rnd() % (n - l)); int64_t s = 0; for (uint32_t i = l; i <= r; i++) s += A[i]; CHECK(P[r + 1] - P[l] == s); }
        memset(D, 0, sizeof(int64_t) * (n + 1)); int64_t naive[600] = {0}, got[600] = {0};
        for (int q = 0; q < 50; q++) { uint32_t l = (uint32_t)(rnd() % n), r = l + (uint32_t)(rnd() % (n - l)); int64_t v = (int64_t)(rnd() % 100) - 50; diff_add(D, l, r, v); for (uint32_t i = l; i <= r; i++) naive[i] += v; }
        diff_apply(D, n, got); for (uint32_t i = 0; i < n; i++) CHECK(got[i] == naive[i]);
    }
    for (int tc = 0; tc < 100; tc++) {
        uint32_t H = (uint32_t)(rnd() % 30) + 1, Wd = (uint32_t)(rnd() % 30) + 1;
        for (uint32_t i = 0; i < H * Wd; i++) A[i] = (int32_t)(rnd() % 200) - 100;
        prefix2d_build(A, H, Wd, P);
        for (int q = 0; q < 100; q++) { uint32_t r1 = (uint32_t)(rnd() % H), r2 = r1 + (uint32_t)(rnd() % (H - r1)), c1 = (uint32_t)(rnd() % Wd), c2 = c1 + (uint32_t)(rnd() % (Wd - c1)); int64_t s = 0;
            for (uint32_t r = r1; r <= r2; r++) for (uint32_t c = c1; c <= c2; c++) s += A[r * Wd + c]; CHECK(rect_sum(P, Wd, r1, c1, r2, c2) == s); }
        memset(D, 0, sizeof(int64_t) * (H + 1) * (Wd + 1)); static int64_t nv[32 * 32]; memset(nv, 0, sizeof nv);
        for (int q = 0; q < 30; q++) { uint32_t r1 = (uint32_t)(rnd() % H), r2 = r1 + (uint32_t)(rnd() % (H - r1)), c1 = (uint32_t)(rnd() % Wd), c2 = c1 + (uint32_t)(rnd() % (Wd - c1)); int64_t v = (int64_t)(rnd() % 10);
            diff2d_add(D, Wd, r1, c1, r2, c2, v); for (uint32_t r = r1; r <= r2; r++) for (uint32_t c = c1; c <= c2; c++) nv[r * Wd + c] += v; }
        diff2d_apply(D, H, Wd); for (uint32_t r = 0; r < H; r++) for (uint32_t c = 0; c < Wd; c++) CHECK(D[r * (Wd + 1) + c] == nv[r * Wd + c]);
    }
    { uint32_t n = 100000; for (uint32_t i = 0; i < n; i++) A[i] = (int32_t)(rnd() % 1000); int64_t s1 = 0, s2 = 0; uint32_t L[1000], R[1000];
      for (int q = 0; q < 1000; q++) { L[q] = (uint32_t)(rnd() % n); R[q] = L[q] + (uint32_t)(rnd() % (n - L[q])); }
      t0 = now(); for (int q = 0; q < 1000; q++) for (uint32_t i = L[q]; i <= R[q]; i++) s1 += A[i]; t1 = now();
      prefix_build(A, n, P); for (int q = 0; q < 1000; q++) s2 += P[R[q] + 1] - P[L[q]]; t2 = now(); CHECK(s1 == s2);
      printf("1000 range sums n=1e5: loop %.5f prefix(incl build) %.5f x%.0f\n", t1 - t0, t2 - t1, (t1 - t0) / (t2 - t1)); }
    // fenwick
    for (int tc = 0; tc < 100; tc++) {
        uint32_t n = (uint32_t)(rnd() % 300) + 1; int64_t naive[301] = {0}; memset(F, 0, sizeof(int64_t) * (n + 1));
        memset(D, 0, sizeof(int64_t) * (n + 2)); memset(E, 0, sizeof(int64_t) * (n + 2)); int64_t nv2[302] = {0};
        for (int q = 0; q < 300; q++) {
            uint32_t i = (uint32_t)(rnd() % n) + 1; int64_t v = (int64_t)(rnd() % 50); bit_add(F, n, i, v); naive[i] += v;
            uint32_t j = (uint32_t)(rnd() % n) + 1; int64_t s = 0; for (uint32_t k = 1; k <= j; k++) s += naive[k]; CHECK(bit_sum(F, j) == s);
            int64_t tot = bit_sum(F, n); if (tot > 0) { int64_t k = (int64_t)(rnd() % (uint64_t)tot) + 1; uint32_t p = 1; int64_t run = naive[1]; while (run < k) run += naive[++p]; CHECK(bit_lower_bound(F, n, k) == p); }
            uint32_t l = (uint32_t)(rnd() % n) + 1, r = l + (uint32_t)(rnd() % (n - l + 1)); int64_t w = (int64_t)(rnd() % 21) - 10; rbit_add(D, E, n, l, r, w); for (uint32_t k = l; k <= r; k++) nv2[k] += w;
            uint32_t x = (uint32_t)(rnd() % (n + 1)); int64_t s2 = 0; for (uint32_t k = 1; k <= x; k++) s2 += nv2[k]; CHECK(rbit_prefix(D, E, x) == s2);
        }
    }
    { uint32_t n = 50000; for (uint32_t i = 0; i < n; i++) U[i] = (uint32_t)(rnd() % n) + 1; uint64_t inv1 = 0, inv2 = 0;
      t0 = now(); for (uint32_t i = 0; i < n; i++) for (uint32_t j = i + 1; j < n; j++) inv1 += U[i] > U[j]; t1 = now();
      memset(F, 0, sizeof(int64_t) * (n + 1)); for (uint32_t i = n; i-- > 0;) { inv2 += (uint64_t)bit_sum(F, U[i] - 1); bit_add(F, n, U[i], 1); } t2 = now(); CHECK(inv1 == inv2);
      printf("inversions n=5e4: O(n^2) %.4f BIT %.5f x%.0f\n", t1 - t0, t2 - t1, (t1 - t0) / (t2 - t1)); }
    // segtree + sparse
    for (int tc = 0; tc < 100; tc++) {
        uint32_t n = (uint32_t)(rnd() % 3000) + 1; for (uint32_t i = 0; i < n; i++) A[i] = (int32_t)rnd();
        seg_build(A, n); if (n <= STN) sparse_build(A, n);
        for (int q = 0; q < 300; q++) { uint32_t l = (uint32_t)(rnd() % n), r = l + (uint32_t)(rnd() % (n - l)); int32_t mn = INT32_MAX; for (uint32_t i = l; i <= r; i++) if (A[i] < mn) mn = A[i];
            CHECK(seg_query(l, r + 1) == mn); if (n <= STN) CHECK(sparse_min(l, r) == mn);
            if (rnd() & 1) { uint32_t i = (uint32_t)(rnd() % n); A[i] = (int32_t)rnd(); seg_set(i, A[i]); if (n <= STN) sparse_build(A, n); } }
        CHECK(seg_query(0, 0) == INT32_MAX);
    }
    { uint32_t n = 65536; for (uint32_t i = 0; i < n; i++) A[i] = (int32_t)rnd(); seg_build(A, n); int64_t s1 = 0, s2 = 0; uint32_t L[2000], R[2000];
      for (int q = 0; q < 2000; q++) { L[q] = (uint32_t)(rnd() % n); R[q] = L[q] + (uint32_t)(rnd() % (n - L[q])); }
      t0 = now(); for (int q = 0; q < 2000; q++) { int32_t mn = INT32_MAX; for (uint32_t i = L[q]; i <= R[q]; i++) if (A[i] < mn) mn = A[i]; s1 += mn; } t1 = now();
      for (int q = 0; q < 2000; q++) s2 += seg_query(L[q], R[q] + 1); t2 = now(); CHECK(s1 == s2);
      printf("2000 range-min n=65536: loop %.5f segtree %.6f x%.0f\n", t1 - t0, t2 - t1, (t1 - t0) / (t2 - t1)); }
    // window max, next greater
    for (int tc = 0; tc < 200; tc++) {
        uint32_t n = (uint32_t)(rnd() % 400) + 1, k = (uint32_t)(rnd() % n) + 1; for (uint32_t i = 0; i < n; i++) A[i] = (int32_t)(rnd() % 50);
        window_max(A, n, k, O1, U); for (uint32_t i = 0; i + k <= n; i++) { int32_t mx = A[i]; for (uint32_t j = i; j < i + k; j++) if (A[j] > mx) mx = A[j]; CHECK(O1[i] == mx); }
        next_greater(A, n, T1, U); for (uint32_t i = 0; i < n; i++) { uint32_t j = i + 1; while (j < n && A[j] <= A[i]) j++; CHECK(T1[i] == j); }
    }
    { uint32_t n = 200000, k = 1000; for (uint32_t i = 0; i < n; i++) A[i] = (int32_t)rnd(); t0 = now();
      for (uint32_t i = 0; i + k <= n; i++) { int32_t mx = A[i]; for (uint32_t j = i; j < i + k; j++) if (A[j] > mx) mx = A[j]; O2[i] = mx; } t1 = now(); window_max(A, n, k, O1, U); t2 = now();
      CHECK(!memcmp(O1, O2, (n - k + 1) * 4)); printf("window max n=2e5 k=1000: naive %.4f deque %.5f x%.0f\n", t1 - t0, t2 - t1, (t1 - t0) / (t2 - t1)); }
    // two pointers, pairs, kadane
    for (int tc = 0; tc < 300; tc++) {
        uint32_t n = (uint32_t)(rnd() % 200) + 1; for (uint32_t i = 0; i < n; i++) U[i] = (uint32_t)(rnd() % 20); uint64_t S = rnd() % 200;
        uint32_t best = 0; for (uint32_t i = 0; i < n; i++) { uint64_t s = 0; for (uint32_t j = i; j < n; j++) { s += U[j]; if (s <= S && j - i + 1 > best) best = j - i + 1; } }
        CHECK(longest_sum_le(U, n, S) == best);
        for (uint32_t i = 0; i < n; i++) A[i] = (int32_t)(rnd() % 30) - 10; for (uint32_t i = 1; i < n; i++) { int32_t x = A[i]; uint32_t j = i; while (j && A[j - 1] > x) { A[j] = A[j - 1]; j--; } A[j] = x; }
        int64_t tg = (int64_t)(rnd() % 40) - 20; uint64_t c = 0; for (uint32_t i = 0; i < n; i++) for (uint32_t j = i + 1; j < n; j++) c += A[i] + A[j] == tg; CHECK(count_pairs_sum(A, n, tg) == c);
        for (uint32_t i = 0; i < n; i++) A[i] = (int32_t)(rnd() % 30) - 15; int64_t mb = A[0]; for (uint32_t i = 0; i < n; i++) { int64_t s = 0; for (uint32_t j = i; j < n; j++) { s += A[j]; if (s > mb) mb = s; } } CHECK(max_subarray(A, n) == mb);
    }
    // lower_bound, bs answer, LIS
    for (int tc = 0; tc < 300; tc++) {
        uint32_t n = (uint32_t)(rnd() % 100); for (uint32_t i = 0; i < n; i++) U[i] = (uint32_t)(rnd() % 50); for (uint32_t i = 1; i < n; i++) { uint32_t x = U[i], j = i; while (j && U[j - 1] > x) { U[j] = U[j - 1]; j--; } U[j] = x; }
        for (uint32_t x = 0; x < 52; x++) { uint32_t p = 0; while (p < n && U[p] < x) p++; CHECK(lower_bound_u32(U, n, x) == p); }
        n = (uint32_t)(rnd() % 50) + 1; for (uint32_t i = 0; i < n; i++) U[i] = (uint32_t)(rnd() % 100); bs_a = U; bs_n = n; bs_k = (uint32_t)(rnd() % n) + 1;
        uint64_t tot = 0; for (uint32_t i = 0; i < n; i++) tot += U[i]; uint64_t ans = bs_answer(0, tot); uint64_t na = 0; while (!ok_split(na)) na++; CHECK(ans == na);
        n = (uint32_t)(rnd() % 60) + 1; for (uint32_t i = 0; i < n; i++) U[i] = (uint32_t)(rnd() % 30); uint32_t L[64], bl = 0; for (uint32_t i = 0; i < n; i++) { L[i] = 1; for (uint32_t j = 0; j < i; j++) if (U[j] < U[i] && L[j] + 1 > L[i]) L[i] = L[j] + 1; if (L[i] > bl) bl = L[i]; }
        CHECK(lis_len(U, n, T1) == bl);
    }
    { uint32_t n = 20000; for (uint32_t i = 0; i < n; i++) U[i] = (uint32_t)rnd(); t0 = now(); uint32_t bl = 0; for (uint32_t i = 0; i < n; i++) { T2[i] = 1; for (uint32_t j = 0; j < i; j++) if (U[j] < U[i] && T2[j] + 1 > T2[i]) T2[i] = T2[j] + 1; if (T2[i] > bl) bl = T2[i]; } t1 = now();
      CHECK(lis_len(U, n, T1) == bl); t2 = now(); printf("LIS n=2e4: O(n^2) %.4f O(nlogn) %.6f x%.0f\n", t1 - t0, t2 - t1, (t1 - t0) / (t2 - t1)); }
    // knapsack
    for (int tc = 0; tc < 300; tc++) {
        uint32_t n = (uint32_t)(rnd() % 12) + 1, C = (uint32_t)(rnd() % 60), w[12], v[12]; for (uint32_t i = 0; i < n; i++) { w[i] = (uint32_t)(rnd() % 15); v[i] = (uint32_t)(rnd() % 20); }
        uint32_t best = 0; for (uint32_t m = 0; m < (1u << n); m++) { uint32_t ww = 0, vv = 0; for (uint32_t i = 0; i < n; i++) if (m >> i & 1) { ww += w[i]; vv += v[i]; } if (ww <= C && vv > best) best = vv; }
        CHECK(knap01(w, v, n, C, T1) == best);
        static uint32_t ub[61]; memset(ub, 0, sizeof ub); for (uint32_t c = 0; c <= C; c++) for (uint32_t i = 0; i < n; i++) if (w[i] && w[i] <= c && ub[c - w[i]] + v[i] > ub[c]) ub[c] = ub[c - w[i]] + v[i];
        int haszero = 0; for (uint32_t i = 0; i < n; i++) if (!w[i] && v[i]) haszero = 1; if (!haszero) CHECK(knap_unbounded(w, v, n, C, T1) == ub[C]);
        uint32_t C2 = (uint32_t)(rnd() % 300); for (uint32_t i = 0; i < n; i++) w[i] = (uint32_t)(rnd() % 70);
        subset_sum_bits(w, n, C2, U2); static uint8_t can[400]; memset(can, 0, sizeof can); for (uint32_t m = 0; m < (1u << n); m++) { uint32_t s = 0; for (uint32_t i = 0; i < n; i++) if (m >> i & 1) s += w[i]; if (s <= C2) can[s] = 1; }
        for (uint32_t s = 0; s <= C2; s++) CHECK(((U2[s >> 5] >> (s & 31)) & 1) == can[s]);
        for (uint32_t s = C2 + 1; s < ((C2 / 32) + 1) * 32; s++) CHECK(((U2[s >> 5] >> (s & 31)) & 1) == 0);
    }
    { uint32_t n = 200, C = 100000, w[200]; for (uint32_t i = 0; i < n; i++) w[i] = (uint32_t)(rnd() % 1000) + 1; static uint8_t bb[100001];
      t0 = now(); memset(bb, 0, sizeof bb); bb[0] = 1; for (uint32_t i = 0; i < n; i++) for (uint32_t c = C; c >= w[i]; c--) bb[c] |= bb[c - w[i]]; t1 = now(); subset_sum_bits(w, n, C, U2); t2 = now();
      int same = 1; for (uint32_t s = 0; s <= C; s++) if (bb[s] != ((U2[s >> 5] >> (s & 31)) & 1)) same = 0; CHECK(same);
      printf("subset-sum n=200 C=1e5: bool dp %.4f bitset %.5f x%.1f\n", t1 - t0, t2 - t1, (t1 - t0) / (t2 - t1)); }
    // tsp
    for (int tc = 0; tc < 40; tc++) {
        uint32_t n = (uint32_t)(rnd() % 8) + 1, d[TSPN][TSPN]; for (uint32_t i = 0; i < n; i++) for (uint32_t j = 0; j < n; j++) d[i][j] = i == j ? 0 : (uint32_t)(rnd() % 100) + 1;
        uint32_t perm[8], best = UINT32_MAX; for (uint32_t i = 0; i < n; i++) perm[i] = i;
        for (;;) { uint32_t c = 0; for (uint32_t i = 0; i + 1 < n; i++) c += d[perm[i]][perm[i + 1]]; c += d[perm[n - 1]][0]; if (n == 1) c = 0; if (c < best) best = c;
            int i = (int)n - 2; while (i >= 1 && perm[i] >= perm[i + 1]) i--; if (i < 1) break; int j = (int)n - 1; while (perm[j] <= perm[i]) j--; uint32_t t = perm[i]; perm[i] = perm[j]; perm[j] = t; for (int a = i + 1, b = (int)n - 1; a < b; a++, b--) { t = perm[a]; perm[a] = perm[b]; perm[b] = t; } }
        CHECK(tsp(n, d) == best);
    }
    { uint32_t n = 11, d[TSPN][TSPN]; for (uint32_t i = 0; i < n; i++) for (uint32_t j = 0; j < n; j++) d[i][j] = i == j ? 0 : (uint32_t)(rnd() % 100) + 1;
      t0 = now(); uint32_t perm[11], best = UINT32_MAX; for (uint32_t i = 0; i < n; i++) perm[i] = i;
      for (;;) { uint32_t c = 0; for (uint32_t i = 0; i + 1 < n; i++) c += d[perm[i]][perm[i + 1]]; c += d[perm[n - 1]][0]; if (c < best) best = c;
          int i = (int)n - 2; while (i >= 1 && perm[i] >= perm[i + 1]) i--; if (i < 1) break; int j = (int)n - 1; while (perm[j] <= perm[i]) j--; uint32_t t = perm[i]; perm[i] = perm[j]; perm[j] = t; for (int a = i + 1, b = (int)n - 1; a < b; a++, b--) { t = perm[a]; perm[a] = perm[b]; perm[b] = t; } }
      t1 = now(); CHECK(tsp(n, d) == best); t2 = now(); printf("TSP n=11: permutations %.4f bitmask dp %.5f x%.0f\n", t1 - t0, t2 - t1, (t1 - t0) / (t2 - t1)); }
    // sos
    for (int tc = 0; tc < 30; tc++) { uint32_t n = (uint32_t)(rnd() % 10) + 1; for (uint32_t m = 0; m < (1u << n); m++) U[m] = U2[m] = (uint32_t)(rnd() % 100);
        sos(U, n); for (uint32_t m = 0; m < (1u << n); m++) { uint32_t s = U2[m]; for (uint32_t sub = m; sub; sub = (sub - 1) & m) if (sub != m) s += U2[sub]; if (m) s += U2[0]; CHECK(U[m] == s); } }
    // mitm
    for (int tc = 0; tc < 200; tc++) { uint32_t n = (uint32_t)(rnd() % 16) + 1, w[16]; for (uint32_t i = 0; i < n; i++) w[i] = (uint32_t)(rnd() % 1000); uint32_t T = (uint32_t)(rnd() % 8000);
        uint64_t c = 0; for (uint32_t m = 0; m < (1u << n); m++) { uint32_t s = 0; for (uint32_t i = 0; i < n; i++) if (m >> i & 1) s += w[i]; c += s <= T; } CHECK(mitm_count_le(w, n, T) == c); }
    { uint32_t n = 26, w[30]; for (uint32_t i = 0; i < n; i++) w[i] = (uint32_t)(rnd() % 1000000); uint32_t T = 6000000; uint64_t c = 0;
      t0 = now(); for (uint32_t m = 0; m < (1u << n); m++) { uint32_t s = 0; for (uint32_t r = m; r; r &= r - 1) s += w[__builtin_ctz(r)]; c += s <= T; } t1 = now(); CHECK(mitm_count_le(w, n, T) == c); t2 = now();
      printf("subset count n=26: brute %.4f mitm %.5f x%.0f\n", t1 - t0, t2 - t1, (t1 - t0) / (t2 - t1));
      uint32_t w30[30]; for (uint32_t i = 0; i < 30; i++) w30[i] = (uint32_t)(rnd() % 1000000); t0 = now(); sink = (int64_t)mitm_count_le(w30, 30, 15000000); t1 = now(); printf("mitm n=30: %.4f s\n", t1 - t0); }
    // compress
    for (int tc = 0; tc < 100; tc++) { uint32_t n = (uint32_t)(rnd() % 500) + 1; for (uint32_t i = 0; i < n; i++) U[i] = U2[i] = (uint32_t)rnd() % 50; uint32_t k = compress(U, n, T1);
        for (uint32_t i = 1; i < k; i++) CHECK(U[i - 1] < U[i]); for (uint32_t i = 0; i < n; i++) { uint32_t id = lower_bound_u32(U, k, U2[i]); CHECK(id < k && U[id] == U2[i]); } }
    // strings
    { static char t[100005], p[64]; static uint32_t pi[64], z[100100], pos[100005];
      for (int tc = 0; tc < 300; tc++) { uint32_t n = (uint32_t)(rnd() % 2000) + 1, m = (uint32_t)(rnd() % 8) + 1, al = (uint32_t)(rnd() % 3) + 1;
        for (uint32_t i = 0; i < n; i++) t[i] = (char)('a' + rnd() % al); for (uint32_t i = 0; i < m; i++) p[i] = (char)('a' + rnd() % al);
        prefix_function(p, m, pi); uint32_t c = kmp_count(t, n, p, m, pi, pos), c2 = 0;
        for (uint32_t i = 0; i + m <= n; i++) if (!memcmp(t + i, p, m)) { CHECK(c2 < c && pos[c2] == i); c2++; } CHECK(c == c2);
        for (uint32_t i = 0; i < m; i++) { uint32_t best = 0; for (uint32_t L = 1; L <= i; L++) if (!memcmp(p, p + i + 1 - L, L)) best = L; CHECK(pi[i] == best); }
        static char s[100100]; memcpy(s, p, m); s[m] = '#'; memcpy(s + m + 1, t, n); z_function(s, n + m + 1, z); uint32_t c3 = 0; for (uint32_t i = m + 1; i < n + m + 1; i++) c3 += z[i] == m; CHECK(c3 == c2);
        for (uint32_t i = 1; i < 60 && i < n; i++) { uint32_t k = 0; while (i + k < n && t[k] == t[i + k]) k++; z_function(t, n, z); CHECK(z[i] == k); }
        static uint32_t H1[2100], P1[2100], H2[2100], P2[2100]; rhash_t a = {H1, P1}, b = {H2, P2}; rhash_build(&a, t, n, 131); rhash_build(&b, t, n, 1000003);
        for (int q = 0; q < 200; q++) { uint32_t len = (uint32_t)(rnd() % 6) + 1; if (len > n) continue; uint32_t i = (uint32_t)(rnd() % (n - len + 1)), j = (uint32_t)(rnd() % (n - len + 1));
            int eq = !memcmp(t + i, t + j, len); CHECK(eq == (rhash_get(&a, i, len) == rhash_get(&a, j, len) && rhash_get(&b, i, len) == rhash_get(&b, j, len))); } }
      for (int i = 0; i < 1000000; i++) { uint64_t x = rnd() >> 2; CHECK(mers_mod(x) == x % MERS); } CHECK(mers_mod((uint64_t)MERS * MERS) == 0);
      uint32_t n = 100000, m = 50; for (uint32_t i = 0; i < n; i++) t[i] = 'a'; for (uint32_t i = 0; i < m; i++) p[i] = 'a'; p[m - 1] = 'b';
      t0 = now(); uint32_t c = 0; for (uint32_t i = 0; i + m <= n; i++) { uint32_t k = 0; while (k < m && t[i + k] == p[k]) k++; c += k == m; } t1 = now(); prefix_function(p, m, pi); uint32_t c2 = kmp_count(t, n, p, m, pi, NULL); t2 = now();
      CHECK(c == c2); printf("search worst case n=1e5 m=50: naive %.5f kmp %.6f x%.0f\n", t1 - t0, t2 - t1, (t1 - t0) / (t2 - t1)); }
    // ntt
    for (int tc = 0; tc < 100; tc++) { uint32_t la = (uint32_t)(rnd() % 300) + 1, lb = (uint32_t)(rnd() % 300) + 1; for (uint32_t i = 0; i < la; i++) U[i] = (uint32_t)(rnd() % NTT_MOD); for (uint32_t i = 0; i < lb; i++) U2[i] = (uint32_t)(rnd() % NTT_MOD);
        uint32_t k = convolve(U, la, U2, lb, T1); CHECK(k == la + lb - 1);
        for (uint32_t i = 0; i < k; i++) { uint64_t s = 0; for (uint32_t j = 0; j <= i; j++) if (j < la && i - j < lb) s = (s + (uint64_t)U[j] * U2[i - j]) % NTT_MOD; CHECK(T1[i] == s); } }
    { uint32_t la = 16000, lb = 16000; for (uint32_t i = 0; i < la; i++) U[i] = (uint32_t)(rnd() % 1000), U2[i] = (uint32_t)(rnd() % 1000);
      t0 = now(); for (uint32_t i = 0; i < la + lb - 1; i++) { uint64_t s = 0; uint32_t jlo = i >= lb ? i - lb + 1 : 0, jhi = i < la ? i : la - 1; for (uint32_t j = jlo; j <= jhi; j++) s += (uint64_t)U[j] * U2[i - j]; T2[i] = (uint32_t)(s % NTT_MOD); } t1 = now();
      convolve(U, la, U2, lb, T1); t2 = now(); CHECK(!memcmp(T1, T2, (la + lb - 1) * 4)); printf("convolution 16000x16000: naive %.4f ntt %.5f x%.0f\n", t1 - t0, t2 - t1, (t1 - t0) / (t2 - t1)); }
    printf("fails=%d\n", fails);
    return fails != 0;
}
