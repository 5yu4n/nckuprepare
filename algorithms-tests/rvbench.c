#include "nt.c"
void *memset(void *d, int c, size_t n) { unsigned char *p = d; while (n--) *p++ = (unsigned char)c; return d; }
void *memcpy(void *d, const void *s, size_t n) { unsigned char *p = d; const unsigned char *q = s; while (n--) *p++ = *q++; return d; }
int memcmp(const void *a, const void *b, size_t n) { const unsigned char *p = a, *q = b; for (; n; n--, p++, q++) if (*p != *q) return *p - *q; return 0; }
static long sys3(long n, long a, long b, long c) { register long a0 asm("a0") = a, a1 asm("a1") = b, a2 asm("a2") = c, a7 asm("a7") = n; asm volatile("ecall" : "+r"(a0) : "r"(a1), "r"(a2), "r"(a7) : "memory"); return a0; }
static void put_u64(uint64_t x) { char b[24]; int i = 23; b[i--] = '\n'; do { b[i--] = (char)('0' + x % 10); x /= 10; } while (x); sys3(64, 1, (long)(b + i + 1), 23 - i); }
static uint32_t xs = 2463534242u;
static uint32_t rnd(void) { xs ^= xs << 13; xs ^= xs >> 17; xs ^= xs << 5; return xs; }
static uint32_t gcd_euclid(uint32_t a, uint32_t b) { while (b) { uint32_t t = a % b; a = b; b = t; } return a; }
static int prime_trial(uint32_t n) { if (n < 2) return 0; for (uint32_t d = 2; d * d <= n && d < 65536; d++) if (n % d == 0) return 0; return 1; }
static uint32_t data[4096];
__attribute__((noinline)) static uint64_t run(void) {
    uint64_t acc = 0;
    for (int i = 0; i < 4096; i++) data[i] = rnd();
    // setup ends here: marker
    asm volatile("nop; nop; nop" ::: "memory");
#if V == 1
    for (int i = 0; i < 4096; i += 2) acc += gcd_euclid(data[i], data[i + 1]);
#elif V == 2
    for (int i = 0; i < 4096; i += 2) acc += gcd_u32(data[i], data[i + 1]);
#elif V == 3
    { uint64_t r = 1, p = 1000000007u; for (int i = 0; i < 4096; i++) r = r * (data[i] % p) % p; acc = r; }
#elif V == 4
    { mont_t M = mont_init(1000000007u); uint32_t r = to_mont(&M, 1); for (int i = 0; i < 4096; i++) r = mont_mul(&M, r, to_mont(&M, data[i])); acc = from_mont(&M, r); }
#elif V == 5
    { barrett_t B = barrett_init(1000000007u); uint64_t r = 1; for (int i = 0; i < 4096; i++) r = barrett_mod(&B, r * barrett_mod(&B, data[i])); acc = r; }
#elif V == 6
    { volatile uint32_t dv = 1234567; uint32_t d = dv; for (int i = 0; i < 4096; i++) acc += data[i] % d + data[i] / d; }
#elif V == 7
    { volatile uint32_t dv = 1234567; uint32_t d = dv; uint64_t M = fd_init(d); for (int i = 0; i < 4096; i++) acc += fastmod_u32(data[i], M, d) + fastdiv_u32(data[i], M); }
#elif V == 8
    for (int i = 0; i < 64; i++) acc += pow_mod(data[i], data[i + 64], 1000000007u);
#elif V == 9
    { mont_t M = mont_init(1000000007u); for (int i = 0; i < 64; i++) acc += pow_mont(&M, data[i], data[i + 64]); }
#elif V == 10
    for (int i = 0; i < 64; i++) acc += prime_trial(data[i] | 0x80000001u);
#elif V == 11
    for (int i = 0; i < 64; i++) acc += is_prime_u32(data[i] | 0x80000001u);
#elif V == 12
    for (uint32_t n = 0; n <= 30000; n++) acc += prime_trial(n);
#elif V == 13
    { uint32_t n = 30000; static uint8_t c[30001]; for (uint32_t i = 2; i * i <= n; i++) if (!c[i]) for (uint32_t j = i * i; j <= n; j += i) c[j] = 1; for (uint32_t i = 2; i <= n; i++) acc += !c[i]; }
#endif
    asm volatile("nop; nop; nop; nop" ::: "memory");
    return acc;
}
void _start(void) { asm volatile(".option push\n.option norelax\nla gp, __global_pointer$\n.option pop"); put_u64(run()); sys3(93, 0, 0, 0); }
