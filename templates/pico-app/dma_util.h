// dma_util.h：DMA 複製 / 填值（pico-sdk）。通道只 claim 一次重複用。
//   dma_copy(dst, src, bytes)            阻塞複製（< 64 bytes 直接 memcpy）
//   int ch = dma_copy_async(...); ...; dma_wait(ch);   搬的同時 CPU 做別的
//   dma_fill32(dst, value, words)        把 value 填滿 dst（取代 memset / 填值迴圈）
// 規則：count 是「筆數」不是位元組；搬完前不要讀 dst、不要改 src；dst/src 要對齊傳輸寬度。
#pragma once
#include <stdint.h>
#include <string.h>
#include "hardware/dma.h"

static int dma_util_ch_copy = -1, dma_util_ch_fill = -1;

static inline int dma_copy_async(void *dst, const void *src, size_t bytes) {
    if (dma_util_ch_copy < 0) dma_util_ch_copy = dma_claim_unused_channel(true);
    uintptr_t a = (uintptr_t)dst | (uintptr_t)src | bytes;
    enum dma_channel_transfer_size sz = (a & 3) == 0 ? DMA_SIZE_32 : (a & 1) == 0 ? DMA_SIZE_16 : DMA_SIZE_8;
    dma_channel_config c = dma_channel_get_default_config(dma_util_ch_copy);
    channel_config_set_transfer_data_size(&c, sz);
    channel_config_set_read_increment(&c, true);
    channel_config_set_write_increment(&c, true);
    dma_channel_configure(dma_util_ch_copy, &c, dst, src, (uint32_t)(bytes >> sz), true);   // 筆數 = bytes >> 0/1/2
    return dma_util_ch_copy;
}
static inline void dma_wait(int ch) { dma_channel_wait_for_finish_blocking((uint)ch); }
static inline void dma_copy(void *dst, const void *src, size_t bytes) {
    if (bytes < 64) { memcpy(dst, src, bytes); return; }
    dma_wait(dma_copy_async(dst, src, bytes));
}

// 填值：讀端固定指向一個 word（read_increment = false），寫端遞增
static uint32_t dma_util_fill_value;
static inline int dma_fill32_async(uint32_t *dst, uint32_t value, uint32_t words) {
    if (dma_util_ch_fill < 0) dma_util_ch_fill = dma_claim_unused_channel(true);
    dma_util_fill_value = value;
    dma_channel_config c = dma_channel_get_default_config(dma_util_ch_fill);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
    channel_config_set_read_increment(&c, false);
    channel_config_set_write_increment(&c, true);
    dma_channel_configure(dma_util_ch_fill, &c, dst, &dma_util_fill_value, words, true);
    return dma_util_ch_fill;
}
static inline void dma_fill32(uint32_t *dst, uint32_t value, uint32_t words) { dma_wait(dma_fill32_async(dst, value, words)); }
