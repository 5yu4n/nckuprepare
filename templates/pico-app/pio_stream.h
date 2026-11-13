// pio_stream.h：DMA → PIO → DMA 串流（S03 的模組版）。CPU 只描述工作，資料經 PIO 變換後寫到 dst。
// 用法（xform.pio 裡的程式，換名字就換變換）：
//   PIO pio = pio0; uint sm = pio_claim_unused_sm(pio, true);
//   uint off = pio_add_program(pio, &xform_bitrev_program);
//   xform_init(pio, sm, off, xform_bitrev_program_get_default_config(off));
//   pio_stream_t s; pio_stream_init(&s, pio, sm);              // claim 兩個 DMA 通道（只做一次）
//   pio_stream_start(&s, in, out, n);  /* CPU 可以做別的 */  pio_stream_wait(&s);
// 規則：先啟動 RX 再啟動 TX；等的是 RX 收完；n 是 word 數。
#pragma once
#include <stdint.h>
#include "hardware/dma.h"
#include "hardware/pio.h"

typedef struct { PIO pio; uint sm; int tx, rx; } pio_stream_t;

static inline void pio_stream_init(pio_stream_t *s, PIO pio, uint sm) {
    s->pio = pio; s->sm = sm;
    s->tx = dma_claim_unused_channel(true);
    s->rx = dma_claim_unused_channel(true);
}
static inline void pio_stream_start(pio_stream_t *s, const uint32_t *src, uint32_t *dst, uint32_t words) {
    dma_channel_config c = dma_channel_get_default_config((uint)s->rx);       // PIO RX FIFO → dst
    channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
    channel_config_set_read_increment(&c, false);
    channel_config_set_write_increment(&c, true);
    channel_config_set_dreq(&c, pio_get_dreq(s->pio, s->sm, false));
    dma_channel_configure((uint)s->rx, &c, dst, &s->pio->rxf[s->sm], words, true);

    c = dma_channel_get_default_config((uint)s->tx);                          // src → PIO TX FIFO
    channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
    channel_config_set_read_increment(&c, true);
    channel_config_set_write_increment(&c, false);
    channel_config_set_dreq(&c, pio_get_dreq(s->pio, s->sm, true));
    dma_channel_configure((uint)s->tx, &c, &s->pio->txf[s->sm], src, words, true);
}
static inline void pio_stream_wait(pio_stream_t *s) { dma_channel_wait_for_finish_blocking((uint)s->rx); }
