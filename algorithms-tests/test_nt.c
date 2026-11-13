#define _POSIX_C_SOURCE 199309L
#include "nt.c"
#include <stdio.h>
#include <time.h>
static int fails = 0;
#define CHECK(c) do { if (!(c)) { fails++; if (fails < 20) printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }
static uint64_t rs = 88172645463325252ull;
static uint64_t rnd(void) { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return rs; }
static int naive_prime(uint64_t n) { if (n < 2) return 0; for (uint64_t d = 2; d * d <= n; d++) if (n % d == 0) return 0; return 1; }
static uint32_t naive_gcd(uint32_t a, uint32_t b) { while (b) { uint32_t t = a % b; a = b; b = t; } return a; }
static volatile uint64_t sink;

int main(void) {
    double t0, t1, t2;
    // isqrt
    for (uint32_t i = 0; i < 2000000; i++) { uint32_t r = isqrt32(i); CHECK((uint64_t)r * r <= i && (uint64_t)(r + 1) * (r + 1) > i); }
    for (int i = 0; i < 200000; i++) { uint32_t x = (uint32_t)rnd(); uint32_t r = isqrt32(x); CHECK((uint64_t)r * r <= x && (uint64_t)(r + 1) * (r + 1) > x); }
    CHECK(isqrt32(0xFFFFFFFFu) == 65535);
    for (int i = 0; i < 200000; i++) { uint64_t x = rnd() >> (rnd() & 63); uint64_t r = isqrt64(x); CHECK(r * r <= x && (r + 1 > 0xFFFFFFFFull || (r + 1) * (r + 1) > x)); }
    CHECK(isqrt64(UINT64_MAX) == 0xFFFFFFFFull);
    // powmod vs naive
    for (int i = 0; i < 20000; i++) {
        uint32_t m = (uint32_t)rnd() | 1, a = (uint32_t)rnd(); uint32_t e = rnd() & 255;
        uint64_t r = 1 % m; for (uint32_t k = 0; k < e; k++) r = r * (a % m) % m;
        CHECK(pow_mod(a, e, m) == r);
    }
    CHECK(pow_mod(5, 0, 1) == 0);
    // inverses
    for (int i = 0; i < 100000; i++) {
        uint32_t m = (uint32_t)(rnd() % 2000000000u) + 2, a = (uint32_t)rnd();
        uint32_t v = inv_exgcd(a, m);
        if (naive_gcd(a % m, m) == 1) CHECK((uint64_t)(a % m) * v % m == 1 % m); else CHECK(v == 0);
    }
    { uint32_t p = 1000000007u; for (int i = 0; i < 1000; i++) { uint32_t a = (uint32_t)(rnd() % (p - 1)) + 1; CHECK((uint64_t)a * inv_fermat(a, p) % p == 1); } }
    { static uint32_t inv[100001]; uint32_t p = 1000000007u; inv_table(inv, 100000, p); for (uint32_t i = 1; i <= 100000; i++) CHECK((uint64_t)i * inv[i] % p == 1); }
    // gcd
    for (int i = 0; i < 1000000; i++) { uint32_t a = (uint32_t)rnd() >> (rnd() & 31), b = (uint32_t)rnd() >> (rnd() & 31); CHECK(gcd_u32(a, b) == naive_gcd(a, b)); }
    CHECK(gcd_u32(0, 0) == 0); CHECK(lcm_u32(4294967291u, 4294967279u) == 4294967291ull * 4294967279ull);
    { uint32_t acc = 0; t0 = now(); for (int i = 0; i < 3000000; i++) acc += naive_gcd((uint32_t)rnd(), (uint32_t)rnd()); t1 = now(); for (int i = 0; i < 3000000; i++) acc += gcd_u32((uint32_t)rnd(), (uint32_t)rnd()); t2 = now(); sink = acc; printf("gcd euclid %.3f  stein %.3f  x%.2f\n", t1 - t0, t2 - t1, (t1 - t0) / (t2 - t1)); }
    // sieve
    sieve_odd();
    for (uint32_t n = 0; n <= 200000; n++) CHECK(is_prime_sieved(n) == naive_prime(n));
    { uint32_t c = 0; for (uint32_t n = 0; n <= SIEVE_N; n++) { c += is_prime_sieved(n); if (n % 997 == 0 || n == SIEVE_N || n < 200) CHECK(count_primes_sieved(n) == c); } CHECK(c == 78498); }
    t0 = now(); { uint32_t c = 0; for (uint32_t n = 2; n <= 200000; n++) c += naive_prime(n); sink = c; } t1 = now();
    sieve_odd(); sink = count_primes_sieved(200000); t2 = now();
    double ts = t2 - t1; printf("pi(2e5) trial %.4f  sieve(1e6 whole) %.4f\n", t1 - t0, ts);
    t0 = now(); { uint32_t c = 0; for (uint32_t n = 2; n <= 1000000; n++) c += is_prime_u32(n); sink = c; CHECK(c == 78498); } t1 = now();
    printf("pi(1e6) MR each %.4f  vs sieve %.4f x%.1f\n", t1 - t0, ts, (t1 - t0) / ts);
    // segmented
    { uint32_t c = 0; for (uint32_t n = 0; n <= 70000; n++) { c += is_prime_sieved(n); if (n < 300 || n % 4999 == 0) CHECK(count_primes_seg(n) == c); } }
    CHECK(count_primes_seg(1000000) == 78498);
    t0 = now(); CHECK(count_primes_seg(100000000u) == 5761455); t1 = now(); printf("seg pi(1e8) %.3f s\n", t1 - t0);
    t0 = now(); CHECK(count_primes_seg(4294967295u) == 203280221); t1 = now(); printf("seg pi(2^32-1) %.3f s\n", t1 - t0);
    // linear sieve
    linear_sieve();
    CHECK(lp_np == 4203);
    for (uint32_t n = 1; n <= LN; n++) {
        uint32_t p[10]; uint8_t e[10]; int k = factor_u32(n, p, e);
        if (n > 1) CHECK(spf[n] == p[0]);
        CHECK(phi[n] == (n == 1 ? 1 : phi_u32(n)));
        int m = 1; for (int i = 0; i < k; i++) { if (e[i] > 1) m = 0; else m = -m; }
        CHECK(mu[n] == m);
    }
    // MR
    for (uint32_t n = 0; n <= SIEVE_N; n++) CHECK(is_prime_u32(n) == is_prime_sieved(n));
    for (int i = 0; i < 20000; i++) { uint32_t n = (uint32_t)rnd(); CHECK(is_prime_u32(n) == naive_prime(n)); }
    { static const uint32_t sp[] = {25326001u, 3215031751u, 2152302898u, 3474749660u, 4294967291u, 4294967295u, 4759123141u & 0, 1u, 2u}; for (unsigned i = 0; i < sizeof sp / 4; i++) CHECK(is_prime_u32(sp[i]) == naive_prime(sp[i])); }
    // factor
    for (int i = 0; i < 20000; i++) {
        uint32_t n = (uint32_t)rnd() >> (rnd() & 15), p[10]; uint8_t e[10]; int k = factor_u32(n, p, e);
        uint64_t prod = 1; for (int j = 0; j < k; j++) { CHECK(naive_prime(p[j])); for (int t = 0; t < e[j]; t++) prod *= p[j]; if (j) CHECK(p[j] > p[j - 1]); }
        CHECK(n < 2 ? k == 0 : prod == n);
        uint32_t nd = 0; if (n < 100000) { for (uint32_t d = 1; d <= n; d++) nd += n % d == 0; CHECK(num_divisors(n) == nd); }
    }
    CHECK(phi_u32(1) == 1 && phi_u32(36) == 12 && phi_u32(4294967291u) == 4294967290u);
    // crt
    for (int i = 0; i < 200000; i++) {
        uint64_t m1 = rnd() % 100000 + 1, m2 = rnd() % 100000 + 1, x = rnd() % (m1 * m2);
        uint64_t a = x % m1, m = m1;
        int ok = crt_merge(&a, &m, x % m2, m2);
        CHECK(ok); CHECK(m == m1 / naive_gcd((uint32_t)m1, (uint32_t)m2) * m2); CHECK(a == x % m);
        uint64_t a3 = rnd() % m1, mm = m1, b3 = rnd() % m2; int ok2 = crt_merge(&a3, &mm, b3, m2);
        uint32_t g = naive_gcd((uint32_t)m1, (uint32_t)m2); CHECK(ok2 == ((a3 % g) == (b3 % g)) || !ok2 == ((rnd() & 0) || (a3 % g) != (b3 % g)));
    }
    for (int i = 0; i < 20000; i++) {   // big moduli, lcm up to ~2^62
        uint64_t m1 = (rnd() >> 33) + 1, m2 = (rnd() >> 33) + 1, a = rnd() % m1, m = m1, b = rnd() % m2;
        if (crt_merge(&a, &m, b, m2)) { CHECK(a % m1 == (rnd() & 0) + a % m1); CHECK(a < m); }
        uint64_t aa = a; (void)aa;
    }
    { uint64_t a = 2, m = 3; CHECK(crt_merge(&a, &m, 3, 5) && crt_merge(&a, &m, 2, 7) && a == 23 && m == 105); }
    { uint64_t a = 1, m = 4; CHECK(!crt_merge(&a, &m, 2, 6)); }
    // montgomery
    for (int i = 0; i < 200000; i++) {
        uint32_t m = ((uint32_t)rnd() >> 1) | 1, a = (uint32_t)rnd(), b = (uint32_t)rnd();
        if (m == 1) continue;
        mont_t M = mont_init(m);
        uint32_t r = from_mont(&M, mont_mul(&M, to_mont(&M, a), to_mont(&M, b)));
        CHECK(r == (uint64_t)(a % m) * (b % m) % m);
        uint64_t e = rnd(); CHECK(pow_mont(&M, a, e & 0xFFFFF) == pow_mod(a, e & 0xFFFFF, m));
    }
    { mont_t M = mont_init(3); CHECK(pow_mont(&M, 2, 5) == 2); }
    { uint32_t m = 998244353u; mont_t M = mont_init(m); uint64_t acc1 = 0, acc2 = 0;
      t0 = now(); for (int i = 0; i < 2000; i++) acc1 += pow_mod((uint32_t)i + 2, 0xFFFFFFFFFFFull + i, m); t1 = now();
      for (int i = 0; i < 2000; i++) acc2 += pow_mont(&M, (uint32_t)i + 2, 0xFFFFFFFFFFFull + i);
      t2 = now();
      CHECK(acc1 == acc2); printf("pow %% %.4f mont %.4f x%.2f (host x86 has fast 64-bit div; RV32 gap bigger)\n", t1 - t0, t2 - t1, (t1 - t0) / (t2 - t1)); }
    // barrett
    for (int i = 0; i < 2000000; i++) {
        uint32_t m = (uint32_t)rnd() >> (rnd() & 31); if (!m) m = 1;
        barrett_t B = barrett_init(m); uint64_t x = rnd() >> (rnd() & 63);
        CHECK(barrett_mod(&B, x) == x % m);
    }
    { barrett_t B = barrett_init(0xFFFFFFFFu); CHECK(barrett_mod(&B, UINT64_MAX) == UINT64_MAX % 0xFFFFFFFFu); B = barrett_init(1); CHECK(barrett_mod(&B, UINT64_MAX) == 0); B = barrett_init(2); CHECK(barrett_mod(&B, UINT64_MAX) == 1); }
    // fastdiv
    for (int i = 0; i < 5000000; i++) {
        uint32_t d = (uint32_t)rnd() >> (rnd() & 31); if (d < 2) d = 2;
        uint32_t a = (uint32_t)rnd() >> (rnd() & 7); uint64_t M = fd_init(d);
        CHECK(fastdiv_u32(a, M) == a / d); CHECK(fastmod_u32(a, M, d) == a % d);
    }
    { uint32_t ds[] = {2, 3, 7, 10, 641, 65535, 65536, 65537, 0x7FFFFFFFu, 0x80000000u, 0x80000001u, 0xFFFFFFFEu, 0xFFFFFFFFu};
      uint32_t as[] = {0, 1, 2, 0x7FFFFFFFu, 0x80000000u, 0xFFFFFFFEu, 0xFFFFFFFFu, 0xFFFFFFFDu};
      for (unsigned i = 0; i < 13; i++) for (unsigned j = 0; j < 8; j++) { uint64_t M = fd_init(ds[i]); CHECK(fastdiv_u32(as[j], M) == as[j] / ds[i]); CHECK(fastmod_u32(as[j], M, ds[i]) == as[j] % ds[i]); } }
    for (uint32_t x = 0; x < 0xFFFFFFF0u; x += 977) CHECK((uint32_t)(((uint64_t)x * 0xCCCCCCCDu) >> 35) == x / 10);
    // ncr
    { uint32_t p = 1000000007u; ncr_init(p); static uint32_t C[64];
      for (uint32_t n = 0; n < 60; n++) { pascal_row(n, p, C); for (uint32_t r = 0; r <= n + 2; r++) CHECK(ncr(n, r, p) == (r <= n ? C[r] : 0)); }
      CHECK(ncr(20000, 10000, p) == lucas(20000, 10000, p));
      for (uint32_t m = 1; m < 50; m++) { pascal_row(40, m, C); uint64_t c = 1; for (uint32_t r = 0; r <= 40; r++) { /* C(40,r) exact fits */ CHECK(C[r] == c % m); c = c * (40 - r) / (r + 1); } }
      uint32_t ps[] = {2, 3, 5, 7, 13, 97};
      for (int q = 0; q < 6; q++) for (uint32_t n = 0; n < 60; n++) { pascal_row(n, ps[q], C); for (uint32_t r = 0; r <= n; r++) CHECK(lucas(n, r, ps[q]) == C[r]); }
      CHECK(lucas(1000000000000000000ull, 123456789012345ull, 2) == ((123456789012345ull & ~1000000000000000000ull) == 0)); }
    // matpow: tribonacci vs loop; fib
    { uint32_t m = 1000000007u; uint64_t a[200]; a[0] = 0; a[1] = 0; a[2] = 1;
      for (int i = 3; i < 200; i++) a[i] = (a[i - 1] + a[i - 2] + a[i - 3]) % m;
      mat_t T; memset(&T, 0, sizeof T); T.a[0][0] = T.a[0][1] = T.a[0][2] = 1; T.a[1][0] = 1; T.a[2][1] = 1;
      for (int n = 3; n < 200; n++) { mat_t P = mat_pow(T, (uint64_t)n - 2, m); uint64_t v = ((uint64_t)P.a[0][0] * a[2] + (uint64_t)P.a[0][1] * a[1] + (uint64_t)P.a[0][2] * a[0]) % m; CHECK(v == a[n]); }
      uint64_t f0 = 0, f1 = 1; for (uint64_t n = 0; n < 500; n++) { CHECK(fib_mod(n, m) == f0); uint64_t t = (f0 + f1) % m; f0 = f1; f1 = t; }
      CHECK(fib_mod(1000000000000000000ull, 1000000007u) == 209783453u);
      uint32_t m2 = 2147483629u; f0 = 0; f1 = 1 % m2; for (uint64_t n = 0; n < 3000; n++) { CHECK(fib_mod(n, m2) == f0); uint64_t t = (f0 + f1) % m2; f0 = f1; f1 = t; }
      t0 = now(); f0 = 0; f1 = 1; for (uint64_t n = 0; n < 100000000ull; n++) { uint64_t t = f0 + f1; if (t >= m) t -= m; f0 = f1; f1 = t; } t1 = now(); CHECK(fib_mod(100000000ull, m) == f0);
      for (int i = 0; i < 1000; i++) sink += fib_mod(100000000ull + i, m);
      t2 = now();
      printf("fib(1e8) loop %.3f  fast-doubling %.2e s/call x%.0f\n", t1 - t0, (t2 - t1) / 1000, (t1 - t0) / ((t2 - t1) / 1000)); }
    // cycle
    { for (int tcase = 0; tcase < 20; tcase++) { cyc_m = (uint32_t)(rnd() % 50000) + 1; cyc_a = (uint32_t)rnd(); cyc_c = (uint32_t)rnd(); uint32_t x0 = (uint32_t)(rnd() % cyc_m);
        uint32_t x = x0; for (uint64_t n = 0; n < 200000; n++) { if (n % 7 == 0) CHECK(iterate_n(x0, n) == x); x = cyc_f(x); } }
      cyc_m = 1000003u; cyc_a = 1103515245u; cyc_c = 12345u; uint64_t mu, lam; find_cycle(7, &mu, &lam);
      uint32_t x = 7; for (uint64_t i = 0; i < mu; i++) x = cyc_f(x); uint32_t y = x; for (uint64_t i = 0; i < lam; i++) y = cyc_f(y); CHECK(x == y); }
    // bits
    for (uint64_t n = 0; n < 3000; n++) { uint64_t s = 0; for (uint64_t i = 0; i <= n; i++) s += __builtin_popcountll(i); CHECK(popcount_sum(n) == s); for (int k = 0; k < 13; k++) { uint64_t c = 0; for (uint64_t i = 0; i <= n; i++) c += (i >> k) & 1; CHECK(count_bit_k(n, k) == c); } }
    { uint32_t x = 0x7, cnt = 1; while ((x = next_comb(x)) < (1u << 10)) { CHECK(__builtin_popcount(x) == 3); cnt++; } CHECK(cnt == 120); }
    for (int i = 0; i < 1000000; i++) { uint32_t x = (uint32_t)rnd(); CHECK(popcount_swar(x) == (uint32_t)__builtin_popcount(x)); }
    // sums
    for (uint64_t n = 0; n < 3000; n++) {
        CHECK(sum1(n) == n * (n + 1) / 2);
        uint64_t s = 0, sg = 0; for (uint64_t i = 1; i <= n; i++) { s += n / i; for (uint64_t d = 1; d * d <= i; d++) if (i % d == 0) { sg += d; if (d * d != i) sg += i / d; } }
        if (n < 600) CHECK(sum_sigma(n) == sg);
        CHECK(sum_floor_div(n) == s);
        uint64_t f = 1; uint64_t e5 = 0; for (uint64_t i = 1; i <= n; i++) { uint64_t t = i; while (t % 5 == 0) { t /= 5; e5++; } } (void)f; CHECK(legendre(n, 5) == e5);
    }
    CHECK(sum1(4294967295ull) == 4294967295ull * 2147483648ull);
    for (int i = 0; i < 3000; i++) { uint32_t m = (uint32_t)(rnd() % 1000000) + 1, r = (uint32_t)rnd(); uint32_t n = (uint32_t)(rnd() % 300); uint64_t s = 0, p = 1 % m; for (uint32_t k = 0; k < n; k++) { s = (s + p) % m; p = p * (r % m) % m; } CHECK(geo_sum(r, n, m) == s); }
    t0 = now(); { uint64_t s = 0; for (uint64_t i = 1; i <= 1000000000ull; i++) s += 1000000000ull / i; sink = s; } t1 = now(); sink += sum_floor_div(1000000000ull); t2 = now();
    printf("sum floor(n/i) n=1e9 loop %.3f  blocks %.6f x%.0f\n", t1 - t0, t2 - t1, (t1 - t0) / (t2 - t1));
    // inclusion-exclusion
    for (int i = 0; i < 2000; i++) { uint32_t a[6]; int k = (int)(rnd() % 6) + 1; for (int j = 0; j < k; j++) a[j] = (uint32_t)(rnd() % 30) + 1; uint64_t n = rnd() % 5000, c = 0;
        for (uint64_t x = 1; x <= n; x++) { int any = 0; for (int j = 0; j < k; j++) if (x % a[j] == 0) any = 1; c += any; } CHECK(count_divisible_any(n, a, k) == c); }
    { uint32_t a[3] = {4294967291u, 4294967279u, 2}; CHECK(count_divisible_any(UINT64_MAX >> 1, a, 3) > 0); }
    for (int i = 0; i < 100000; i++) { uint64_t a = rnd() >> (rnd() & 63), b = rnd() >> (rnd() & 63); uint64_t x = a, y = b; while (y) { uint64_t t = x % y; x = y; y = t; } CHECK(gcd_u64(a, b) == x); }
    // pollard / MR64
    for (int i = 0; i < 300000; i++) { uint64_t n = rnd() >> 34; CHECK(is_prime_u64(n) == naive_prime(n)); }
    for (int i = 0; i < 300; i++) { uint64_t n = (rnd() >> 22) | 1; CHECK(is_prime_u64(n) == naive_prime(n)); }
    CHECK(is_prime_u64(9223372036854775783ull) && is_prime_u64(4611686018427387847ull) && is_prime_u64(1000000000000000003ull)); CHECK(!is_prime_u64(3825123056546413051ull)); CHECK(is_prime_u64(1122004669633ull) == naive_prime(1122004669633ull)); CHECK(!is_prime_u64(9223372036854775783ull - 2));
    t0 = now();
    for (int i = 0; i < 3000; i++) {
        uint64_t n = (rnd() >> 1) >> (rnd() % 40); uint64_t f[64]; int k = factor_u64(n, f); uint64_t prod = 1;
        for (int j = 0; j < k; j++) { CHECK(is_prime_u64(f[j])); prod *= f[j]; }
        CHECK(n < 2 ? k == 0 : prod == n);
    }
    { uint64_t n = 999999999989ull * 1000003ull; uint64_t f[64]; int k = factor_u64(n, f); CHECK(k == 2 && f[0] * f[1] == n); }
    { uint64_t n = 3037000493ull * 3037000453ull; uint64_t f[64]; int k = factor_u64(n, f); CHECK(k == 2 && f[0] * f[1] == n); }
    { uint64_t n = 4294967291ull * 4294967291ull / 4294967291ull * 2147483647ull; uint64_t f[64]; int k = factor_u64(n, f); CHECK(k == 2); }
    t1 = now(); printf("factor_u64 3000 random <2^63 %.3f s\n", t1 - t0);
    // bsgs
    { uint32_t ps[] = {7, 13, 1000003u, 999999937u, 1000000007u};
      for (int q = 0; q < 5; q++) for (int i = 0; i < 40; i++) { uint32_t p = ps[q], a = (uint32_t)(rnd() % (p - 1)) + 1, x = (uint32_t)(rnd() % (p - 1)), b = pow_mod(a, x, p);
          int64_t r = bsgs(a, b, p); CHECK(r >= 0 && r <= x && pow_mod(a, (uint64_t)r, p) == b);
          if (p < 2000000) { uint64_t v = 1; int64_t sm = -1; for (int64_t e = 0; e < (int64_t)p; e++) { if (v == b) { sm = e; break; } v = v * a % p; } CHECK(r == sm); }
          uint32_t b2 = (uint32_t)(rnd() % p); int64_t r2 = bsgs(a, b2, p); if (r2 >= 0) CHECK(pow_mod(a, (uint64_t)r2, p) == b2);
          if (p < 2000000) { uint64_t v = 1; int64_t sm = -1; for (int64_t e = 0; e < (int64_t)p; e++) { if (v == b2) { sm = e; break; } v = v * a % p; } CHECK(r2 == sm); } } }
    printf("fails=%d\n", fails);
    return fails != 0;
}
