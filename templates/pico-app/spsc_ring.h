// spsc_ring.h（S08）
#pragma once
// 單生產者單消費者無鎖環形佇列（C11 atomics；RP2350 兩核與 Linux 都能用）
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#define RING_CAP 256u                                 // 必須是 2 的次方
typedef struct {
    _Alignas(64) atomic_uint head;                    // 生產者寫（Linux 上分開快取行）
    _Alignas(64) atomic_uint tail;                    // 消費者寫
    uint32_t buf[RING_CAP];
} ring_t;
static inline bool ring_push(ring_t *r, uint32_t v) {
    unsigned h = atomic_load_explicit(&r->head, memory_order_relaxed);
    unsigned t = atomic_load_explicit(&r->tail, memory_order_acquire);
    if (h - t == RING_CAP) return false;              // 滿
    r->buf[h & (RING_CAP - 1)] = v;
    atomic_store_explicit(&r->head, h + 1, memory_order_release);   // 資料先寫，再公開
    return true;
}
static inline bool ring_pop(ring_t *r, uint32_t *v) {
    unsigned t = atomic_load_explicit(&r->tail, memory_order_relaxed);
    unsigned h = atomic_load_explicit(&r->head, memory_order_acquire);
    if (h == t) return false;                         // 空
    *v = r->buf[t & (RING_CAP - 1)];
    atomic_store_explicit(&r->tail, t + 1, memory_order_release);
    return true;
}
// 批次版：一次搬多筆，少碰共享索引（快很多）
static inline unsigned ring_pop_many(ring_t *r, uint32_t *out, unsigned max) {
    unsigned t = atomic_load_explicit(&r->tail, memory_order_relaxed);
    unsigned h = atomic_load_explicit(&r->head, memory_order_acquire);
    unsigned n = h - t; if (n > max) n = max;
    for (unsigned i = 0; i < n; i++) out[i] = r->buf[(t + i) & (RING_CAP - 1)];
    atomic_store_explicit(&r->tail, t + n, memory_order_release);
    return n;
}
