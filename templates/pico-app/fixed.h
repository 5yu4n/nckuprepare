// fixed.h：定點數工具（不用 float）。RP2350 RISC-V 沒有 FPU，float 運算是軟體函式，慢 10 倍以上。
// 板子與 Linux 都能用；templates/tests/test_fixed.c 已對 libm 比對誤差。
//
//   Q16.16：int32_t，值 = x / 65536。範圍約 ±32768，解析度 1.5e-5。一般小數運算用這個。
//   Q1.15 ：int16_t，值 = x / 32768。範圍 [-1, 1)。訊號、係數、sin/cos 用這個。
//
// 換算規則：float f → Q16 = (int32_t)(f * 65536)；整數 n → Q16 = n << 16。
// 乘法要先轉 int64 再右移，不然會溢位。除法很貴（64 位元除法是軟體函式），熱迴圈改成乘倒數。
#pragma once
#include <stdint.h>

typedef int32_t q16_t;
typedef int16_t q15_t;

#define Q16_ONE        65536
#define Q16_FROM_INT(n) ((q16_t)((int32_t)(n) * Q16_ONE))
#define Q16_FROM_F(f)  ((q16_t)((f) * 65536.0 + ((f) >= 0 ? 0.5 : -0.5)))   // 只在初始化或常數用
#define Q16_TO_F(q)    ((double)(q) / 65536.0)                                // 只在除錯印出用
#define Q15_FROM_F(f)  ((q15_t)((f) >= 0.999969 ? 32767 : (f) * 32768.0 + ((f) >= 0 ? 0.5 : -0.5)))

static inline int32_t q16_to_int(q16_t a)        { return a >> 16; }                 // 往負無限大取整
static inline int32_t q16_round(q16_t a)         { return (a + 32768) >> 16; }        // 四捨五入
static inline q16_t   q16_mul(q16_t a, q16_t b)  { return (q16_t)(((int64_t)a * b) >> 16); }   // RV32：mul + mulh
static inline q16_t   q16_mul_round(q16_t a, q16_t b) { return (q16_t)(((int64_t)a * b + 32768) >> 16); }
static inline q16_t   q16_div(q16_t a, q16_t b)  { return (q16_t)(((int64_t)a * 65536) / b); } // 慢：64 位元除法，迴圈外用

// 飽和加減：結果超出範圍就卡在最大/最小，不會繞回
static inline int32_t sat_add32(int32_t a, int32_t b) {
    int32_t r;
    if (__builtin_add_overflow(a, b, &r)) return a > 0 ? INT32_MAX : INT32_MIN;
    return r;
}
static inline int32_t sat_sub32(int32_t a, int32_t b) {
    int32_t r;
    if (__builtin_sub_overflow(a, b, &r)) return a >= 0 ? INT32_MAX : INT32_MIN;
    return r;
}
static inline int16_t sat16(int32_t x) { return (int16_t)(x > 32767 ? 32767 : x < -32768 ? -32768 : x); }

// Q1.15 乘法（四捨五入；-1 × -1 飽和成 32767）
static inline q15_t q15_mul(q15_t a, q15_t b) { return sat16(((int32_t)a * b + (1 << 14)) >> 15); }

// 夾住、絕對值、線性內插（t 是 Q16，0..65536）
static inline int32_t clamp32(int32_t x, int32_t lo, int32_t hi) { return x < lo ? lo : x > hi ? hi : x; }   // RV32 會變 min/max
static inline int32_t abs32(int32_t x) { return x < 0 ? -x : x; }
static inline q16_t   q16_lerp(q16_t a, q16_t b, q16_t t) { return a + q16_mul(b - a, t); }

// 整數平方根：floor(sqrt(x))，只用移位與加減，不用除法
static inline uint32_t isqrt32(uint32_t x) {
    uint32_t r = 0, bit = 1u << 30;
    while (bit > x) bit >>= 2;
    while (bit) {
        if (x >= r + bit) { x -= r + bit; r = (r >> 1) + bit; }
        else r >>= 1;
        bit >>= 2;
    }
    return r;
}
static inline uint32_t isqrt64(uint64_t x) {
    uint64_t r = 0, bit = 1ull << 62;
    while (bit > x) bit >>= 2;
    while (bit) {
        if (x >= r + bit) { x -= r + bit; r = (r >> 1) + bit; }
        else r >>= 1;
        bit >>= 2;
    }
    return (uint32_t)r;
}
// Q16 平方根：sqrt(a/65536) * 65536 = sqrt(a * 65536)
static inline q16_t q16_sqrt(q16_t a) { return a <= 0 ? 0 : (q16_t)isqrt64((uint64_t)a << 16); }

// 向量長度 hypot（Q16 或整數都可，不溢位版本）
static inline uint32_t ihypot(int32_t x, int32_t y) { return isqrt64((uint64_t)((int64_t)x * x) + (uint64_t)((int64_t)y * y)); }

// ---- sin / cos 查表（Q1.15 輸出）----
// 角度用 uint16_t：0..65535 = 0..360 度（自然繞回，不用取模）。
// 1025 格的 1/4 週期表 + 線性內插，實測最大誤差 1.42 LSB（約 4e-5），見 tests/test_fixed.c。
// 表格是 static：多個 .c 都 include 時各有一份，各自要 init（只在一個 .c 用最省事）。
// 用之前呼叫一次 fx_sin_init()（裡面用 float sinf，只跑一次）；或用 FX_SIN_TABLE_CONST 換成編譯期表格。
#define FX_SIN_BITS 10
static q15_t fx_sin_tab[(1 << FX_SIN_BITS) + 1];
#include <math.h>
static inline void fx_sin_init(void) {
    for (int i = 0; i <= (1 << FX_SIN_BITS); i++) {
        double v = sin((double)i * (3.14159265358979323846 / 2) / (1 << FX_SIN_BITS));
        int32_t q = (int32_t)(v * 32768.0 + 0.5);
        fx_sin_tab[i] = (q15_t)(q > 32767 ? 32767 : q);
    }
}
static inline q15_t fx_sin(uint16_t ang) {
    uint32_t quad = ang >> 14;                      // 第幾象限
    uint32_t x = ang & 0x3FFF;                      // 象限內 0..16383
    if (quad & 1) x = 0x4000 - x;                   // 2、4 象限鏡射
    uint32_t idx = x >> (14 - FX_SIN_BITS);
    uint32_t frac = x & ((1u << (14 - FX_SIN_BITS)) - 1);
    int32_t a = fx_sin_tab[idx];
    int32_t b = idx < (1u << FX_SIN_BITS) ? fx_sin_tab[idx + 1] : a;
    int32_t v = a + (((b - a) * (int32_t)frac) >> (14 - FX_SIN_BITS));
    return (q15_t)(quad & 2 ? -v : v);
}
static inline q15_t fx_cos(uint16_t ang) { return fx_sin((uint16_t)(ang + 0x4000)); }
// 度數轉角度單位：deg * 65536 / 360（常數乘除，編譯器會轉成乘法）
static inline uint16_t fx_deg(int32_t deg) { return (uint16_t)(((int64_t)deg * 65536) / 360); }
