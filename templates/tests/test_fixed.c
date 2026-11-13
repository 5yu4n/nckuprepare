// gcc -O2 -I../pico-app test_fixed.c -lm && ./a.out
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include "fixed.h"
static uint32_t s = 12345;
static uint32_t rnd(void) { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
int main(void) {
    int fails = 0;
    // isqrt32 / isqrt64 exact
    for (uint64_t i = 0; i < 2000000; i++) { uint32_t x = (uint32_t)i; uint32_t r = isqrt32(x);
        if ((uint64_t)r * r > x || (uint64_t)(r + 1) * (r + 1) <= x) { printf("isqrt32 %u\n", x); fails++; break; } }
    for (int i = 0; i < 2000000; i++) { uint32_t x = rnd(); uint32_t r = isqrt32(x);
        if ((uint64_t)r * r > x || (uint64_t)(r + 1) * (r + 1) <= x) { printf("isqrt32 %u\n", x); fails++; break; } }
    uint32_t edge[] = {0, 1, 2, 3, 4, 0xFFFFFFFFu, 0xFFFE0001u, 0xFFFE0000u};
    for (unsigned i = 0; i < 8; i++) { uint32_t x = edge[i], r = isqrt32(x);
        if ((uint64_t)r * r > x || (uint64_t)(r + 1) * (r + 1) <= x) { printf("isqrt32 edge %u\n", x); fails++; } }
    for (int i = 0; i < 2000000; i++) { uint64_t x = ((uint64_t)rnd() << 32) | rnd(); if (i < 64) x = ~0ull >> i; uint64_t r = isqrt64(x);
        if (r * r > x || ((r + 1) * (r + 1) <= x && r != 0xFFFFFFFFull)) { printf("isqrt64 %llu\n", (unsigned long long)x); fails++; break; } }
    // q16 mul vs double
    double maxe = 0;
    for (int i = 0; i < 1000000; i++) { q16_t a = (q16_t)(rnd() % 20000000) - 10000000, b = (q16_t)(rnd() % 2000000) - 1000000;
        double e = fabs(Q16_TO_F(q16_mul_round(a, b)) - Q16_TO_F(a) * Q16_TO_F(b)); if (e > maxe) maxe = e; }
    printf("q16_mul_round max err %.3g (1 LSB = %.3g)\n", maxe, 1.0 / 65536); if (maxe > 1.0 / 65536) fails++;
    // q16_sqrt
    maxe = 0;
    for (int i = 0; i < 1000000; i++) { q16_t a = (q16_t)(rnd() & 0x7FFFFFFF); double e = fabs(Q16_TO_F(q16_sqrt(a)) - sqrt(Q16_TO_F(a))); if (e > maxe) maxe = e; }
    printf("q16_sqrt max err %.3g\n", maxe); if (maxe > 2.0 / 65536) fails++;
    // q15_mul
    if (q15_mul(-32768, -32768) != 32767) { puts("q15 sat"); fails++; }
    for (int a = -32768; a < 32768; a += 7) for (int b = -32768; b < 32768; b += 331) {
        double ex = (double)a * b / 32768.0; int r = q15_mul(a, b); if (ex > 32767) ex = 32767;
        if (fabs(r - ex) > 0.5 + 1e-9) { printf("q15 %d %d %d %f\n", a, b, r, ex); fails++; goto q15done; } }
q15done:
    // sat
    if (sat_add32(INT32_MAX, 1) != INT32_MAX || sat_add32(INT32_MIN, -1) != INT32_MIN || sat_sub32(INT32_MIN, 1) != INT32_MIN || sat_sub32(0, INT32_MIN) != INT32_MAX || sat_add32(5, -7) != -2) { puts("sat"); fails++; }
    // sin/cos
    fx_sin_init(); maxe = 0;
    for (uint32_t a = 0; a < 65536; a++) { double ex = sin(a * 2 * M_PI / 65536) * 32768.0; if (ex > 32767) ex = 32767;
        double e = fabs(fx_sin((uint16_t)a) - ex); if (e > maxe) maxe = e;
        double ec = fabs(fx_cos((uint16_t)a) - fmin(cos(a * 2 * M_PI / 65536) * 32768.0, 32767)); if (ec > maxe) maxe = ec; }
    printf("fx_sin/cos max err %.2f LSB\n", maxe); if (maxe > 3) fails++;
    if (fx_deg(90) != 16384 || fx_deg(360) != 0 || fx_deg(-90) != 49152) { puts("deg"); fails++; }
    if (ihypot(3, 4) != 5 || ihypot(-30000, 40000) != 50000 || ihypot(INT32_MIN, INT32_MIN) != 3037000499u) { printf("hypot %u\n", ihypot(INT32_MIN, INT32_MIN)); fails++; }
    printf("fails=%d\n", fails); return fails != 0;
}
