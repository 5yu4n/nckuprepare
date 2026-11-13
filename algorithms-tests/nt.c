#include <stdint.h>
#include <string.h>
//@@ isqrt
// floor(sqrt(n))，純整數（RISC-V 沒有 FPU 也能用）
static uint32_t isqrt32(uint32_t n) {
    uint32_t r = 0, bit = 1u << 30;
    while (bit > n) bit >>= 2;
    while (bit) {
        if (n >= r + bit) { n -= r + bit; r = (r >> 1) + bit; }
        else r >>= 1;
        bit >>= 2;
    }
    return r;
}
static uint64_t isqrt64(uint64_t n) {
    uint64_t r = 0, bit = 1ull << 62;
    while (bit > n) bit >>= 2;
    while (bit) {
        if (n >= r + bit) { n -= r + bit; r = (r >> 1) + bit; }
        else r >>= 1;
        bit >>= 2;
    }
    return r;
}
//@@ powmod
// a^e mod m，m < 2^32，O(log e)。注意 RV32 上 uint64 % 是函式呼叫(__umoddi3)，熱迴圈改用 Montgomery
static uint32_t pow_mod(uint32_t a, uint64_t e, uint32_t m) {
    uint64_t r = 1 % m, b = a % m;
    while (e) {
        if (e & 1) r = r * b % m;
        b = b * b % m;
        e >>= 1;
    }
    return (uint32_t)r;
}
//@@ inverse
// p 是質數、a % p != 0：a^(p-2) mod p
static uint32_t inv_fermat(uint32_t a, uint32_t p) { return pow_mod(a, p - 2, p); }
// 任意 m：gcd(a,m)=1 才有反元素，沒有就回傳 0（迭代延伸歐幾里得，不遞迴）
static uint32_t inv_exgcd(uint32_t a, uint32_t m) {
    int64_t r0 = a % m, r1 = m, s0 = 1, s1 = 0;
    while (r1) {
        int64_t q = r0 / r1, t;
        t = r0 - q * r1; r0 = r1; r1 = t;
        t = s0 - q * s1; s0 = s1; s1 = t;
    }
    if (r0 != 1) return 0;
    s0 %= (int64_t)m; if (s0 < 0) s0 += m;
    return (uint32_t)s0;
}
// 1..n 的反元素一次算完，O(n)：inv[i] = -(p/i) * inv[p%i]。p 質數、n < p
static void inv_table(uint32_t *inv, uint32_t n, uint32_t p) {
    inv[1] = 1;
    for (uint32_t i = 2; i <= n; i++)
        inv[i] = (uint32_t)((uint64_t)(p - p / i) * inv[p % i] % p);
}
//@@ gcd
// 二進位 GCD (Stein)：只有位移與減法，ctz 在 Hazard3(Zbb) 是一道指令
static uint32_t gcd_u32(uint32_t a, uint32_t b) {
    if (!a) return b;
    if (!b) return a;
    int sh = __builtin_ctz(a | b);
    a >>= __builtin_ctz(a);
    do {
        b >>= __builtin_ctz(b);
        if (a > b) { uint32_t t = a; a = b; b = t; }
        b -= a;
    } while (b);
    return a << sh;
}
static uint64_t lcm_u32(uint32_t a, uint32_t b) {   // 先除再乘，避免溢位
    return (a && b) ? (uint64_t)(a / gcd_u32(a, b)) * b : 0;
}
//@@ sieve_odd
// 只存奇數的位元篩：bit i 代表 2i+1。RAM = N/16 bytes（N=4e6 → 250 KB）
#define SIEVE_N 1000000u
static uint32_t sieve_bits[(SIEVE_N / 2) / 32 + 1];   // 1 = 合數
static void sieve_odd(void) {
    memset(sieve_bits, 0, sizeof sieve_bits);
    sieve_bits[0] |= 1u;                                // 1 不是質數
    for (uint32_t i = 3; i * i <= SIEVE_N; i += 2)
        if (!((sieve_bits[i >> 6] >> ((i >> 1) & 31)) & 1))
            for (uint32_t j = i * i; j <= SIEVE_N; j += 2 * i)
                sieve_bits[j >> 6] |= 1u << ((j >> 1) & 31);
}
static int is_prime_sieved(uint32_t n) {          // n <= SIEVE_N
    if (n < 2) return 0;
    if (!(n & 1)) return n == 2;
    return !((sieve_bits[n >> 6] >> ((n >> 1) & 31)) & 1);
}
static uint32_t count_primes_sieved(uint32_t n) { // π(n)，用 popcount 一次數 32 個
    if (n < 2) return 0;
    uint32_t last = (n - 1) >> 1, c = 1, w;       // c=1 先算 2
    for (w = 0; w < (last >> 5); w++) c += __builtin_popcount(~sieve_bits[w]);
    uint32_t rem = (last & 31) + 1;
    uint32_t mask = rem == 32 ? 0xFFFFFFFFu : (1u << rem) - 1;
    return c + __builtin_popcount(~sieve_bits[w] & mask);
}
//@@ sieve_seg
// 分段篩：π(N)，N 可到 2^32-1，RAM 只要約 75 KB（整段篩 N=1e8 要 6 MB，放不下）
#define SEG 16384u
static uint32_t count_primes_seg(uint32_t N) {
    static uint8_t seg[SEG], small[32768];       // seg[i]: 奇數 2(lo+i)+1；small[i]: 2i+1
    static uint32_t bp[6542];
    uint32_t nbp = 0, r = isqrt32(N), cnt = 1;
    if (N < 2) return 0;
    memset(small, 0, sizeof small);
    for (uint32_t i = 3; i <= r; i += 2)
        if (!small[i >> 1]) {
            bp[nbp++] = i;
            for (uint32_t j = i * i; j <= r; j += 2 * i) small[j >> 1] = 1;
        }
    uint32_t kmax = (N - 1) / 2;                 // 奇數 2k+1, k = 1..kmax
    for (uint32_t lo = 1; lo <= kmax; lo += SEG) {
        uint32_t hi = (kmax - lo < SEG - 1) ? kmax : lo + SEG - 1;
        memset(seg, 0, hi - lo + 1);
        for (uint32_t t = 0; t < nbp; t++) {
            uint64_t p = bp[t], s = p * p, lowv = 2ull * lo + 1;
            if (s > 2ull * hi + 1) break;
            if (s < lowv) { s = (lowv + p - 1) / p * p; if (!(s & 1)) s += p; }
            for (uint32_t k = (uint32_t)((s - 1) / 2); k <= hi; k += (uint32_t)p) seg[k - lo] = 1;
        }
        for (uint32_t i = 0; i <= hi - lo; i++) cnt += !seg[i];
        if (hi == kmax) break;
    }
    return cnt;
}
//@@ sieve_linear
// 線性篩 O(N)：同時得到質數表、最小質因數 spf、歐拉 φ、Möbius μ
// RAM：spf 4N + phi 4N + mu N bytes。N=40000 → 約 380 KB。不需要的陣列刪掉
#define LN 40000u
static uint32_t lp_primes[4300], lp_np;          // π(40000)=4203
static uint32_t spf[LN + 1], phi[LN + 1];
static int8_t mu[LN + 1];
static void linear_sieve(void) {
    lp_np = 0; phi[1] = 1; mu[1] = 1;
    for (uint32_t i = 2; i <= LN; i++) {
        if (!spf[i]) { spf[i] = i; lp_primes[lp_np++] = i; phi[i] = i - 1; mu[i] = -1; }
        for (uint32_t j = 0; j < lp_np; j++) {
            uint32_t p = lp_primes[j], k = i * p;
            if (p > spf[i] || k > LN) break;
            spf[k] = p;
            if (p == spf[i]) { phi[k] = phi[i] * p; mu[k] = 0; }
            else { phi[k] = phi[i] * (p - 1); mu[k] = (int8_t)-mu[i]; }
        }
    }
}
// 有 spf 之後，分解 n <= LN 只要 O(質因數個數)
//@@ millerrabin
// 32 位元確定性質數判斷：底數 {2,7,61} 對所有 n < 4,759,123,141 正確
static int is_prime_u32(uint32_t n) {
    static const uint8_t sp[] = {2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37};
    static const uint32_t base[] = {2, 7, 61};
    if (n < 2) return 0;
    for (int i = 0; i < 12; i++) {
        if (n == sp[i]) return 1;
        if (n % sp[i] == 0) return 0;
    }
    if (n < 41 * 41) return 1;
    uint32_t d = n - 1;
    int s = __builtin_ctz(d);
    d >>= s;
    for (int i = 0; i < 3; i++) {
        uint64_t x = pow_mod(base[i], d, n);
        if (x == 1 || x == n - 1) continue;
        int ok = 0;
        for (int r = 1; r < s && !ok; r++) { x = x * x % n; ok = (x == n - 1); }
        if (!ok) return 0;
    }
    return 1;
}
//@@ factor
// 試除法分解 n < 2^32（最多 9 個不同質因數）；O(sqrt n) 最壞約 2 萬次除法
static int factor_u32(uint32_t n, uint32_t p[10], uint8_t e[10]) {
    int k = 0;
    if (n < 2) return 0;
    for (uint32_t d = 2; d <= 3; d++)
        if (n % d == 0) { p[k] = d; e[k] = 0; while (n % d == 0) { n /= d; e[k]++; } k++; }
    for (uint32_t d = 5; (uint64_t)d * d <= n; d += 6)       // 6k±1
        for (uint32_t q = d; q <= d + 2; q += 2)
            if (n % q == 0) { p[k] = q; e[k] = 0; while (n % q == 0) { n /= q; e[k]++; } k++; }
    if (n > 1) { p[k] = n; e[k] = 1; k++; }
    return k;
}
// 由分解得到：因數個數 Π(e+1)、因數和 Π(p^(e+1)-1)/(p-1)、φ(n) = n Π(1-1/p)
static uint32_t phi_u32(uint32_t n) {
    uint32_t p[10]; uint8_t e[10];
    int k = factor_u32(n, p, e);
    uint32_t r = n;
    for (int i = 0; i < k; i++) r = r / p[i] * (p[i] - 1);
    return r;
}
static uint32_t num_divisors(uint32_t n) {
    uint32_t p[10], r = 1; uint8_t e[10];
    int k = factor_u32(n, p, e);
    for (int i = 0; i < k; i++) r *= e[i] + 1u;
    return r;
}
//@@ crt
// 沒有 __int128（RV32 GCC 不支援）時的 64 位元 a*b mod m，m < 2^63
static uint64_t mulmod_u64(uint64_t a, uint64_t b, uint64_t m) {
    uint64_t r = 0;
    a %= m; b %= m;
    while (b) {
        if (b & 1) { r += a; if (r >= m) r -= m; }
        a += a; if (a >= m) a -= m;
        b >>= 1;
    }
    return r;
}
static int64_t exgcd64(int64_t a, int64_t b, int64_t *x) {   // 回傳 g，且 a*x ≡ g (mod b)
    int64_t x0 = 1, x1 = 0;
    while (b) {
        int64_t q = a / b, t = a - q * b; a = b; b = t;
        t = x0 - q * x1; x0 = x1; x1 = t;
    }
    *x = x0;
    return a;
}
// 合併 x≡*a (mod *m) 與 x≡a2 (mod m2)，模數不必互質。無解回傳 0。結果 lcm 要 < 2^62
static int crt_merge(uint64_t *a, uint64_t *m, uint64_t a2, uint64_t m2) {
    uint64_t a1 = *a % *m, m1 = *m;
    a2 %= m2;
    int64_t x;
    uint64_t g = (uint64_t)exgcd64((int64_t)(m1 % m2), (int64_t)m2, &x);
    uint64_t diff = (a2 + m2 - a1 % m2) % m2;
    if (diff % g) return 0;
    uint64_t m2g = m2 / g;
    int64_t xm = x % (int64_t)m2g; if (xm < 0) xm += (int64_t)m2g;
    uint64_t t = mulmod_u64(diff / g, (uint64_t)xm, m2g);
    *m = m1 * m2g;
    *a = (a1 + mulmod_u64(m1, t, *m)) % *m;
    return 1;
}
//@@ montgomery
// Montgomery 乘法：m 奇數且 < 2^31。只用 32x32→64 乘法，不需要任何除法
// 用法：先 to_mont 轉進去，迴圈內都用 mont_mul，最後 from_mont 轉回來
typedef struct { uint32_t m, minv, r2; } mont_t;   // minv = -m^{-1} mod 2^32, r2 = 2^64 mod m
static mont_t mont_init(uint32_t m) {
    mont_t M; uint32_t inv = m;                    // 奇數 m：m*m ≡ 1 (mod 8)，牛頓法每次精度加倍
    for (int i = 0; i < 4; i++) inv *= 2 - m * inv;
    M.m = m; M.minv = 0u - inv;
    M.r2 = (uint32_t)((0ull - m) % m);             // 只在初始化做一次 64 位元 %
    return M;
}
static inline uint32_t mont_redc(const mont_t *M, uint64_t T) {   // T < m*2^32 → T/R mod m
    uint32_t u = (uint32_t)T * M->minv;
    uint32_t t = (uint32_t)((T + (uint64_t)u * M->m) >> 32);
    return t >= M->m ? t - M->m : t;
}
static inline uint32_t mont_mul(const mont_t *M, uint32_t a, uint32_t b) { return mont_redc(M, (uint64_t)a * b); }
static inline uint32_t to_mont(const mont_t *M, uint32_t a) { return mont_mul(M, a % M->m, M->r2); }
static inline uint32_t from_mont(const mont_t *M, uint32_t a) { return mont_redc(M, a); }
static uint32_t pow_mont(const mont_t *M, uint32_t a, uint64_t e) {
    uint32_t r = to_mont(M, 1), b = to_mont(M, a);
    while (e) { if (e & 1) r = mont_mul(M, r, b); b = mont_mul(M, b, b); e >>= 1; }
    return from_mont(M, r);
}
//@@ barrett
// Barrett：x mod m，x < 2^64、m 執行期常數(>=1)。4 次 32x32 乘法取代 __umoddi3
static inline uint64_t mulhi64(uint64_t a, uint64_t b) {         // (a*b) >> 64，不用 __int128
    uint64_t al = (uint32_t)a, ah = a >> 32, bl = (uint32_t)b, bh = b >> 32;
    uint64_t ll = al * bl, lh = al * bh, hl = ah * bl, hh = ah * bh;
    uint64_t mid = (ll >> 32) + (uint32_t)lh + (uint32_t)hl;
    return hh + (lh >> 32) + (hl >> 32) + (mid >> 32);
}
typedef struct { uint64_t mu; uint32_t m; } barrett_t;
static barrett_t barrett_init(uint32_t m) { barrett_t B; B.mu = UINT64_MAX / m; B.m = m; return B; }
static inline uint32_t barrett_mod(const barrett_t *B, uint64_t x) {
    uint64_t r = x - mulhi64(x, B->mu) * B->m;                  // r < 3m
    while (r >= B->m) r -= B->m;
    return (uint32_t)r;
}
//@@ fastdiv
// Lemire fastmod/fastdiv：32 位元 a 除以執行期常數 d (d >= 2)，結果完全正確
// 迴圈外 M = fd_init(d)；迴圈內 a/d、a%d 都只剩乘法
static inline uint64_t fd_init(uint32_t d) { return UINT64_MAX / d + 1; }
static inline uint32_t mulhi_64x32(uint64_t M, uint32_t a) {     // (M*a) >> 64
    return (uint32_t)(((M >> 32) * a + (((uint64_t)(uint32_t)M * a) >> 32)) >> 32);
}
static inline uint32_t fastdiv_u32(uint32_t a, uint64_t M) { return mulhi_64x32(M, a); }
static inline uint32_t fastmod_u32(uint32_t a, uint64_t M, uint32_t d) { return mulhi_64x32(M * a, d); }
// 編譯期常數除法編譯器自己會換成乘法，例如 x/10 == (uint32_t)(((uint64_t)x * 0xCCCCCCCDu) >> 35)
//@@ ncr
// nCr mod p（p 質數且 > FN）：O(FN) 預處理，查詢 O(1)。RAM = 8*FN bytes（FN=20000 → 160 KB）
#define FN 20000u
static uint32_t fact[FN + 1], ifact[FN + 1];
static void ncr_init(uint32_t p) {
    fact[0] = 1;
    for (uint32_t i = 1; i <= FN; i++) fact[i] = (uint32_t)((uint64_t)fact[i - 1] * i % p);
    ifact[FN] = pow_mod(fact[FN], p - 2, p);
    for (uint32_t i = FN; i > 0; i--) ifact[i - 1] = (uint32_t)((uint64_t)ifact[i] * i % p);
}
static uint32_t ncr(uint32_t n, uint32_t r, uint32_t p) {
    if (r > n) return 0;
    return (uint32_t)((uint64_t)fact[n] * ifact[r] % p * ifact[n - r] % p);
}
// n 很大、p 很小（例如 p < 1e5）：Lucas 定理
static uint32_t ncr_small(uint32_t n, uint32_t r, uint32_t p) {  // n, r < p，O(r)
    if (r > n) return 0;
    uint64_t num = 1, den = 1;
    for (uint32_t i = 0; i < r; i++) { num = num * (n - i) % p; den = den * (i + 1) % p; }
    return (uint32_t)(num * pow_mod((uint32_t)den, p - 2, p) % p);
}
static uint32_t lucas(uint64_t n, uint64_t r, uint32_t p) {
    uint64_t res = 1;
    while (n || r) {
        uint32_t ni = n % p, ri = r % p;
        if (ri > ni) return 0;
        res = res * ncr_small(ni, ri, p) % p;
        n /= p; r /= p;
    }
    return (uint32_t)res;
}
// 模數不是質數、n 小（<= 60 左右）：巴斯卡三角形一列，C[k] = C(n,k) mod m，任何 m
static void pascal_row(uint32_t n, uint32_t m, uint32_t *C) {
    C[0] = 1 % m;
    for (uint32_t i = 1; i <= n; i++) {
        C[i] = 0;
        for (uint32_t k = i; k > 0; k--) { C[k] += C[k - 1]; if (C[k] >= m) C[k] -= m; }
    }
}
//@@ matpow
// 線性遞迴 a[n] = c1 a[n-1] + ... + cK a[n-MATK]，n 到 1e18：MATK×MATK 矩陣快速冪，O(MATK^3 log n)
#define MATK 3
typedef struct { uint32_t a[MATK][MATK]; } mat_t;
static mat_t mat_mul(const mat_t *x, const mat_t *y, uint32_t m) {
    mat_t r;
    for (int i = 0; i < MATK; i++)
        for (int j = 0; j < MATK; j++) {
            uint64_t s = 0;
            for (int k = 0; k < MATK; k++) s = (s + (uint64_t)x->a[i][k] * y->a[k][j]) % m;
            r.a[i][j] = (uint32_t)s;
        }
    return r;
}
static mat_t mat_pow(mat_t b, uint64_t e, uint32_t m) {
    mat_t r;
    memset(&r, 0, sizeof r);
    for (int i = 0; i < MATK; i++) r.a[i][i] = 1 % m;
    while (e) { if (e & 1) r = mat_mul(&r, &b, m); b = mat_mul(&b, &b, m); e >>= 1; }
    return r;
}
// 用法：轉移矩陣第一列 = {c1..cK}，下面是位移的單位矩陣；
// [a(n) a(n-1) .. a(n-MATK+1)]^T = T^(n-MATK+1) [a(MATK-1) .. a(0)]^T
// 只要 Fibonacci：快速倍增更快，F(2k)=F(k)(2F(k+1)-F(k))、F(2k+1)=F(k)^2+F(k+1)^2，m < 2^31
static uint32_t fib_mod(uint64_t n, uint32_t m) {
    uint64_t a = 0, b = 1 % m;                    // F(0), F(1)
    if (n == 0) return 0;
    for (int i = 63 - __builtin_clzll(n); i >= 0; i--) {
        uint64_t c = a * ((2 * b + m - a) % m) % m;
        uint64_t d = (a * a + b * b) % m;
        if ((n >> i) & 1) { a = d; b = (c + d) % m; } else { a = c; b = d; }
    }
    return (uint32_t)a;
}
//@@ cycle
// 迭代 x→f(x) 的循環偵測（Brent，O(1) 記憶體）：前 mu 項不重複，之後週期 lam
// 「第 1e18 次迭代的值」「LCG 第 n 項」「Pisano 週期」都用這招
static uint32_t cyc_a = 1103515245u, cyc_c = 12345u, cyc_m = 1000003u;
static uint32_t cyc_f(uint32_t x) { return (uint32_t)(((uint64_t)cyc_a * x + cyc_c) % cyc_m); }   // ← 換成題目的 f
static void find_cycle(uint32_t x0, uint64_t *mu, uint64_t *lam) {
    uint64_t power = 1, l = 1, m = 0;
    uint32_t tort = x0, hare = cyc_f(x0);
    while (tort != hare) {
        if (power == l) { tort = hare; power <<= 1; l = 0; }
        hare = cyc_f(hare); l++;
    }
    tort = hare = x0;
    for (uint64_t i = 0; i < l; i++) hare = cyc_f(hare);
    while (tort != hare) { tort = cyc_f(tort); hare = cyc_f(hare); m++; }
    *mu = m; *lam = l;
}
static uint32_t iterate_n(uint32_t x0, uint64_t n) {          // f^n(x0)
    uint64_t mu, lam, steps;
    find_cycle(x0, &mu, &lam);
    steps = n < mu ? n : mu + (n - mu) % lam;
    while (steps--) x0 = cyc_f(x0);
    return x0;
}
//@@ bits
// 0..n 之中第 k 位元為 1 的個數，O(1)
static uint64_t count_bit_k(uint64_t n, int k) {
    uint64_t blk = 1ull << (k + 1), half = 1ull << k, c = (n + 1) / blk * half;
    uint64_t rem = (n + 1) % blk;
    return c + (rem > half ? rem - half : 0);
}
static uint64_t popcount_sum(uint64_t n) {       // Σ_{i=0..n} popcount(i)，O(log n)
    uint64_t s = 0;
    for (int k = 0; k < 63 && (n >> k); k++) s += count_bit_k(n, k);
    return s;
}
// Gosper：下一個 popcount 相同的較大整數（列舉 n 選 k 的所有遮罩）
static uint32_t next_comb(uint32_t x) {
    uint32_t c = x & (0u - x), r = x + c;
    return (((r ^ x) >> 2) / c) | r;
}
// 其他：x&-x 最低位；x&(x-1) 清最低位；!(x&(x-1)) 是否 2 的冪；
// 列舉 m 的所有非空子集：for (s = m; s; s = (s - 1) & m)
// SWAR popcount（沒有 cpop 指令時）
static uint32_t popcount_swar(uint32_t x) {
    x = x - ((x >> 1) & 0x55555555u);
    x = (x & 0x33333333u) + ((x >> 2) & 0x33333333u);
    x = (x + (x >> 4)) & 0x0F0F0F0Fu;
    return (x * 0x01010101u) >> 24;
}
//@@ sums
// 公式取代迴圈：Σi = n(n+1)/2、Σi² = n(n+1)(2n+1)/6、Σi³ = (n(n+1)/2)²（先除再乘避免溢位）
static uint64_t sum1(uint64_t n) { return (n & 1) ? n * ((n + 1) / 2) : (n / 2) * (n + 1); }
// 等比 1 + r + ... + r^(n-1) mod m，任何 m 都行（不用反元素），O(log n)
static uint32_t geo_sum(uint32_t r, uint64_t n, uint32_t m) {
    uint64_t S = 0, P = 1 % m;                    // S = S(k), P = r^k，從 k=0 開始
    r %= m;
    for (int i = 63; i >= 0; i--) {
        S = S * (1 + P) % m; P = P * P % m;     // k → 2k
        if ((n >> i) & 1) { S = (S + P) % m; P = P * r % m; }   // k → k+1
    }
    return (uint32_t)S;
}
// Σ_{i=1..n} floor(n/i)，O(sqrt n)：floor(n/i) 只有約 2 sqrt(n) 種值，同值一段一起算
static uint64_t sum_floor_div(uint64_t n) {
    uint64_t s = 0;
    for (uint64_t i = 1, j; i <= n; i = j + 1) {
        uint64_t q = n / i;
        j = n / q;                                // 最後一個 floor(n/x) == q 的 x
        s += q * (j - i + 1);
    }
    return s;
}
// Σ_{i=1..n} σ(i)（因數和的總和）= Σ_d d*floor(n/d)，同樣分段，O(sqrt n)
static uint64_t sum_sigma(uint64_t n) {
    uint64_t s = 0;
    for (uint64_t i = 1, j; i <= n; i = j + 1) {
        uint64_t q = n / i;
        j = n / q;
        s += q * (sum1(j) - sum1(i - 1));
    }
    return s;
}
// n! 中質數 p 的次方數（Legendre）；n! 尾端 0 的個數 = legendre(n, 5)
static uint64_t legendre(uint64_t n, uint64_t p) { uint64_t e = 0; while (n) { n /= p; e += n; } return e; }
//@@ incl_excl
static uint64_t gcd_u64(uint64_t a, uint64_t b) {
    if (!a) return b;
    if (!b) return a;
    int sh = __builtin_ctzll(a | b);
    a >>= __builtin_ctzll(a);
    do {
        b >>= __builtin_ctzll(b);
        if (a > b) { uint64_t t = a; a = b; b = t; }
        b -= a;
    } while (b);
    return a << sh;
}
// 排容：1..n 中「能被 a[0..k-1] 至少一個整除」的個數，O(2^k · k)。lcm 超過 n 那項是 0，直接跳過
static uint64_t count_divisible_any(uint64_t n, const uint32_t *a, int k) {
    int64_t total = 0;
    for (uint32_t s = 1; s < (1u << k); s++) {
        uint64_t l = 1; int bits = 0, over = 0;
        for (int i = 0; i < k && !over; i++)
            if ((s >> i) & 1) {
                uint64_t q = l / gcd_u64(l, a[i]);
                bits++;
                if (q > n / a[i]) over = 1; else l = q * a[i];
            }
        if (over) continue;
        total += (bits & 1) ? (int64_t)(n / l) : -(int64_t)(n / l);
    }
    return (uint64_t)total;
}
// 互質計數：1..n 中與 x 互質的個數 = n - count_divisible_any(n, x 的相異質因數, k)
//@@ pollard
// 64 位元：Montgomery 乘法（用 mulhi64，不需要 __int128）→ Miller-Rabin → Pollard rho（Brent 版）
typedef struct { uint64_t m, minv, one, r2; } mont64_t;
static mont64_t mont64_init(uint64_t m) {          // m 奇數 < 2^63
    mont64_t M; uint64_t inv = m, r;
    for (int i = 0; i < 5; i++) inv *= 2 - m * inv;
    M.m = m; M.minv = 0 - inv;
    M.one = (0 - m) % m;                            // R = 2^64 mod m
    r = M.one;
    for (int i = 0; i < 64; i++) r = (r >= m - r) ? r - (m - r) : r + r;   // R^2 mod m
    M.r2 = r;
    return M;
}
static inline uint64_t m64_mul(const mont64_t *M, uint64_t a, uint64_t b) {
    uint64_t lo = a * b, hi = mulhi64(a, b), u = lo * M->minv;
    uint64_t t = hi + mulhi64(u, M->m) + (lo != 0);
    return t >= M->m ? t - M->m : t;
}
static inline uint64_t m64_to(const mont64_t *M, uint64_t a) { return m64_mul(M, a % M->m, M->r2); }
static int is_prime_u64(uint64_t n) {
    static const uint64_t base[] = {2, 325, 9375, 28178, 450775, 9780504, 1795265022};  // 對所有 64 位元正確
    if (n < 4294967296ull) return is_prime_u32((uint32_t)n);
    if (!(n & 1)) return 0;
    mont64_t M = mont64_init(n);
    uint64_t d = n - 1, mone = n - M.one;          // mone = -1 的 Montgomery 形式
    int s = __builtin_ctzll(d);
    d >>= s;
    for (int i = 0; i < 7; i++) {
        uint64_t a = base[i] % n, x = M.one, b, e = d;
        if (!a) continue;
        b = m64_to(&M, a);
        while (e) { if (e & 1) x = m64_mul(&M, x, b); b = m64_mul(&M, b, b); e >>= 1; }
        if (x == M.one || x == mone) continue;
        int ok = 0;
        for (int r = 1; r < s && !ok; r++) { x = m64_mul(&M, x, x); ok = (x == mone); }
        if (!ok) return 0;
    }
    return 1;
}
static inline uint64_t rho_f(const mont64_t *M, uint64_t v, uint64_t cm) {   // v^2 + c
    uint64_t t = m64_mul(M, v, v) + cm;
    return t >= M->m ? t - M->m : t;
}
static uint64_t rho(uint64_t n, uint64_t c) {      // n 奇合數；回傳一個因數（可能等於 n，換 c 重試）
    mont64_t M = mont64_init(n);
    uint64_t cm = m64_to(&M, c), y = M.one, x = y, ys = y, q = M.one, g = 1;
    for (uint64_t r = 1; g == 1; r <<= 1) {
        x = y;
        for (uint64_t i = 0; i < r; i++) y = rho_f(&M, y, cm);
        for (uint64_t k = 0; k < r && g == 1; k += 128) {
            ys = y;
            for (uint64_t i = 0; i < 128 && i < r - k; i++) {
                y = rho_f(&M, y, cm);
                q = m64_mul(&M, q, x > y ? x - y : y - x);
            }
            g = gcd_u64(q, n);
        }
    }
    if (g == n)
        do { ys = rho_f(&M, ys, cm); g = gcd_u64(x > ys ? x - ys : ys - x, n); } while (g == 1);
    return g;
}
// 分解 n < 2^63：質因數（含重複、未排序）寫進 out，回傳個數（<= 63）。用陣列當堆疊，不遞迴
static int factor_u64(uint64_t n, uint64_t *out) {
    uint64_t st[64]; int k = 0, sp = 0;
    if (n < 2) return 0;
    while (!(n & 1)) { out[k++] = 2; n >>= 1; }
    for (uint64_t p = 3; p < 100 && p * p <= n; p += 2)
        while (n % p == 0) { out[k++] = p; n /= p; }
    if (n > 1) st[sp++] = n;
    while (sp) {
        uint64_t x = st[--sp], d = x;
        if (is_prime_u64(x)) { out[k++] = x; continue; }
        for (uint64_t c = 1; d == x; c++) d = rho(x, c);
        st[sp++] = d; st[sp++] = x / d;
    }
    return k;
}
//@@ bsgs
// 離散對數：找最小 x >= 0 使 a^x ≡ b (mod p)，p 質數，O(sqrt p)。找不到回傳 -1
// 用開放定址雜湊表存 baby steps：RAM = 8 * HT bytes
#define HT (1u << 16)                             // 要 >= 2*sqrt(p)，p < 2^31 → 2^16 夠用到 p≈1e9
static uint32_t ht_key[HT], ht_val[HT];
static uint8_t ht_used[HT];
static int64_t bsgs(uint32_t a, uint32_t b, uint32_t p) {
    uint32_t n = isqrt32(p) + 1;
    a %= p; b %= p;
    if (1 % p == b) return 0;
    memset(ht_used, 0, sizeof ht_used);
    uint64_t cur = b;                             // baby: b * a^j，j = 0..n-1
    for (uint32_t j = 0; j < n; j++) {
        uint32_t h = (uint32_t)(cur * 2654435761u) & (HT - 1);
        while (ht_used[h] && ht_key[h] != cur) h = (h + 1) & (HT - 1);
        ht_used[h] = 1; ht_key[h] = (uint32_t)cur; ht_val[h] = j;   // 同 key 留最大的 j
        cur = cur * a % p;
    }
    uint32_t an = pow_mod(a, n, p);
    uint64_t g = an;                              // giant: a^(n*i)，i = 1..n
    for (uint32_t i = 1; i <= n; i++) {
        uint32_t h = (uint32_t)(g * 2654435761u) & (HT - 1);
        while (ht_used[h]) {
            if (ht_key[h] == g) return (int64_t)i * n - ht_val[h];
            h = (h + 1) & (HT - 1);
        }
        g = g * an % p;
    }
    return -1;
}
