#include <stdint.h>
#include <string.h>
//@@ kmp
// KMP：在文字 t 中找樣式 p 的所有出現位置，O(n+m) 取代 O(nm)。pi 大小 m
// pi[i] = p[0..i] 最長「既是前綴也是後綴」的長度（真前綴）
static void prefix_function(const char *p, uint32_t m, uint32_t *pi) {
    pi[0] = 0;
    for (uint32_t i = 1, k = 0; i < m; i++) {
        while (k && p[i] != p[k]) k = pi[k - 1];
        if (p[i] == p[k]) k++;
        pi[i] = k;
    }
}
static uint32_t kmp_count(const char *t, uint32_t n, const char *p, uint32_t m, const uint32_t *pi, uint32_t *pos) {
    uint32_t cnt = 0;
    for (uint32_t i = 0, k = 0; i < n; i++) {
        while (k && t[i] != p[k]) k = pi[k - 1];
        if (t[i] == p[k]) k++;
        if (k == m) { if (pos) pos[cnt] = i + 1 - m; cnt++; k = pi[k - 1]; }
    }
    return cnt;
}
// 最小週期：字串 s 由長度 L = m - pi[m-1] 的片段重複而成 ⇔ m % L == 0
//@@ zfunc
// Z 函數：z[i] = s 與 s[i..] 的最長共同前綴，O(n)。找樣式：對 p + '#' + t 算 z，z == |p| 的位置就是出現
static void z_function(const char *s, uint32_t n, uint32_t *z) {
    uint32_t l = 0, r = 0;
    if (!n) return;
    z[0] = n;
    for (uint32_t i = 1; i < n; i++) {
        uint32_t k = 0;
        if (i < r) { k = r - i; if (z[i - l] < k) k = z[i - l]; }
        while (i + k < n && s[k] == s[i + k]) k++;
        z[i] = k;
        if (i + k > r) { l = i; r = i + k; }
    }
}
//@@ hash
// 滾動雜湊：子字串比較 O(1)。模 2^31-1（梅森質數）用位移取代 %，沒有任何除法
// 兩組不同底數 = 雙雜湊，碰撞機率約 n^2 / 2^62。RAM：每組 H、P 各 4(n+1) bytes
#define MERS 0x7FFFFFFFu
static inline uint32_t mers_mod(uint64_t x) {            // x < 2^62
    x = (x & MERS) + (x >> 31);
    x = (x & MERS) + (x >> 31);
    return (uint32_t)(x >= MERS ? x - MERS : x);
}
typedef struct { uint32_t *H, *P; } rhash_t;              // H[i] = s[0..i-1] 的雜湊，P[i] = base^i
static void rhash_build(rhash_t *h, const char *s, uint32_t n, uint32_t base) {
    h->H[0] = 0; h->P[0] = 1;
    for (uint32_t i = 0; i < n; i++) {
        h->H[i + 1] = mers_mod((uint64_t)h->H[i] * base + (uint8_t)s[i] + 1);
        h->P[i + 1] = mers_mod((uint64_t)h->P[i] * base);
    }
}
static inline uint32_t rhash_get(const rhash_t *h, uint32_t l, uint32_t len) {   // s[l .. l+len-1]
    uint32_t a = h->H[l + len], b = mers_mod((uint64_t)h->H[l] * h->P[len]);
    return a >= b ? a - b : a + MERS - b;
}
// 用法：rhash_build(&h1, s, n, 131); rhash_build(&h2, s, n, 1000003);
// 兩個子字串相等 ⇔ 兩組雜湊都相等。最長重複子字串 = 對長度二分 + 雜湊（排序後找相同）
//@@ ntt
// NTT 卷積（多項式乘法、大數乘法、「所有 a_i + b_j 的出現次數」）：O(n log n) 取代 O(n^2)
// 模 998244353 = 119·2^23+1，原根 3。用前面的 Montgomery 乘法，迴圈內沒有除法
// 係數結果要 < 998244353（計數題通常成立）。RAM = 8 × NTTN bytes（2^15 → 256 KB）
#define NTT_MOD 998244353u
#define NTTN (1u << 15)
static uint32_t na_[NTTN], nb_[NTTN];
static void ntt(uint32_t *a, uint32_t n, int invert, const mont_t *M) {   // a 為 Montgomery 形式
    for (uint32_t i = 1, j = 0; i < n; i++) {                              // 位元反轉排列
        uint32_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) { uint32_t t = a[i]; a[i] = a[j]; a[j] = t; }
    }
    for (uint32_t len = 2; len <= n; len <<= 1) {
        uint32_t w = pow_mod(3, (NTT_MOD - 1) / len, NTT_MOD);
        if (invert) w = pow_mod(w, NTT_MOD - 2, NTT_MOD);
        uint32_t wm = to_mont(M, w), half = len >> 1;
        for (uint32_t i = 0; i < n; i += len) {
            uint32_t wc = to_mont(M, 1);
            for (uint32_t j = 0; j < half; j++) {
                uint32_t u = a[i + j], v = mont_mul(M, a[i + j + half], wc);
                a[i + j] = u + v >= NTT_MOD ? u + v - NTT_MOD : u + v;
                a[i + j + half] = u >= v ? u - v : u + NTT_MOD - v;
                wc = mont_mul(M, wc, wm);
            }
        }
    }
    if (invert) {
        uint32_t ninv = to_mont(M, pow_mod(n, NTT_MOD - 2, NTT_MOD));
        for (uint32_t i = 0; i < n; i++) a[i] = mont_mul(M, a[i], ninv);
    }
}
// out[k] = Σ a[i] b[k-i]，長度 la+lb-1，必須 <= NTTN
static uint32_t convolve(const uint32_t *a, uint32_t la, const uint32_t *b, uint32_t lb, uint32_t *out) {
    mont_t M = mont_init(NTT_MOD);
    uint32_t need = la + lb - 1, n = 1;
    while (n < need) n <<= 1;
    memset(na_, 0, n * sizeof na_[0]); memset(nb_, 0, n * sizeof nb_[0]);
    for (uint32_t i = 0; i < la; i++) na_[i] = to_mont(&M, a[i]);
    for (uint32_t i = 0; i < lb; i++) nb_[i] = to_mont(&M, b[i]);
    ntt(na_, n, 0, &M); ntt(nb_, n, 0, &M);
    for (uint32_t i = 0; i < n; i++) na_[i] = mont_mul(&M, na_[i], nb_[i]);
    ntt(na_, n, 1, &M);
    for (uint32_t i = 0; i < need; i++) out[i] = from_mont(&M, na_[i]);
    return need;
}
