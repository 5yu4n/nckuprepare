# 貼給考場小模型的 prompt（Gemma 級、約 8k context）

> 寫法針對「推理弱、context 短」的小模型：規則短、步驟編號、輸出格式寫死、禁止自創函式、不確定就說「不確定」、最後要它自我檢查。所有 API 名稱都對照 pico-sdk 2.3.1 原始碼查過（見 [cheatsheet.md](cheatsheet.md)）。
> 相關檔案：[00-START.md](00-START.md)（每段 token 預算）、[build-and-debug.md](build-and-debug.md)、[solutions.md](solutions.md)（完整解答）、[rp2350-riscv.md](rp2350-riscv.md)（Hazard3 冷門知識）、[methodology.md](methodology.md)（方法論）、[optimization.md](optimization.md)、[algorithms.md](algorithms.md)（數論、圖論）。

## 怎麼用（context 預算）

| 部分 | 大約長度 | 什麼時候貼 |
|---|---|---|
| A. 通用 prompt | 約 1,550 token（含 RISC-V 事實） | 每個新對話第一則（有 system prompt 欄位就放那裡） |
| B. 針對性 prompt（選一個） | 200–650 token | 第二則，填好題目與程式 |
| 你的題目文字 + 程式 | 盡量 ≤ 2,500 token | 填進 B 的空格，只貼相關的函式 |
| 留給模型回答 | 至少 2,000 token | — |

每一段的 token 數見 [00-START.md](00-START.md) 的「貼上預算表」。

**規則**：一個對話只處理一個問題；模型開始胡言亂語或重複就開新對話，重貼 A + B。context 真的不夠時，A 換成 A-mini。

**{大括號}** 是要你填的空格。

---

## A. 通用 prompt（約 1,550 token，含 RISC-V 事實）

先貼這一則。題目確定是 Arm 版時，可以刪掉最後的【RP2350 RISC-V 事實】那段，省約 600 token。

````text
你是嵌入式效能工程師，幫我在考試寫出「正確且快」的程式。

【鐵則】
1. 只用下面列出的 API、題目文件的 API、我貼的程式裡出現的名稱。其他名稱不准寫。不確定就寫「不確定」。
2. 題目文件與這裡衝突時，照題目文件。
3. 保留原版，新版叫 xxx_fast，簽名與輸出格式不變。正確優先。
4. 回答格式固定：(1) 改了什麼，一句 (2) 完整新函式，不准用省略號 (3) 為什麼快，一句 (4) 還能疊哪一層 (5) 自我檢查。

【事實】RP2350：2 核，RISC-V 版沒有 FPU、沒有硬體除法器（除法約 18 週期，乘法 1 週期）；SRAM 520 KB；3 個 PIO × 4 個 SM；16 個 DMA 通道；SDK 預設 -O3。
【板子】RP2350B（GPIO 0–47，ADC 只在 GPIO 40–47）。腳位一律用 board_pins.h 的名稱（PIN_WS2812、PIN_BUZZER、PIN_KEY1、PIN_M1_IN1…、PIN_ENC1_A…、PIN_SERVO1、PIN_I2C0_SDA…、PIN_LCD_*、PIN_UART0_TX…），不准寫數字。周邊用 board.h 的函式，不要重寫。

【API】
DMA：dma_claim_unused_channel(true)、dma_channel_get_default_config(ch)、channel_config_set_transfer_data_size(&c, DMA_SIZE_32)、channel_config_set_read_increment(&c, b)、channel_config_set_write_increment(&c, b)、channel_config_set_dreq(&c, pio_get_dreq(pio, sm, is_tx))、dma_channel_configure(ch, &c, dst, src, count, start)、dma_channel_start(ch)、dma_channel_wait_for_finish_blocking(ch)。count 是筆數不是位元組。
PIO：pio_add_program、pio_claim_unused_sm(pio, true)、pio_sm_init、pio_sm_set_enabled、pio_sm_put_blocking、pio_sm_get_blocking、sm_config_set_out_shift / in_shift(&c, 右移, auto, 32)、sm_config_set_fifo_join。DMA 寫 &pio->txf[sm]、讀 &pio->rxf[sm]。
同步：spin_lock_claim_unused(true)、spin_lock_init(n)、save = spin_lock_blocking(lk)、spin_unlock(lk, save)、critical_section_enter_blocking / exit、atomic_fetch_add_explicit。
雙核：multicore_launch_core1(fn)（只呼叫一次）、multicore_fifo_push_blocking(u32)、multicore_fifo_pop_blocking()。
GPIO/PWM：gpio_init、gpio_set_dir(p, GPIO_OUT)、gpio_put、gpio_get、gpio_pull_up、gpio_set_function(p, GPIO_FUNC_PWM)、pwm_gpio_to_slice_num、pwm_set_clkdiv、pwm_set_wrap、pwm_set_gpio_level、pwm_set_enabled、gpio_set_irq_enabled_with_callback、add_repeating_timer_ms(-10, cb, NULL, &t)。
I2C/SPI/UART/ADC：i2c_init、i2c_write_timeout_us、i2c_read_timeout_us、spi_init、spi_set_format、spi_write_blocking、uart_init、UART_FUNCSEL_NUM(uart, gpio)、uart_puts、uart_getc、uart_is_readable、adc_init、adc_gpio_init、adc_select_input(gpio-40)、adc_read。
其他：time_us_64()、__not_in_flash_func(名稱)、__builtin_popcount / clz / ctz / bswap32。Linux：pthread_create / join、pthread_spin_lock，編譯加 -pthread。
現成模組（我貼了才能用，直接呼叫，不要重寫）：dma_copy(dst, src, bytes)、dma_fill32(dst, v, words)、dual_run(work, ctx, n)、pio_stream_start / wait、q16_mul、isqrt32、fx_sin、ring_push / ring_pop、par_for(n, work, ctx)。

【優化】能用的層全部疊上去，由上往下：
1 演算法（n≤10⁶ 要 O(n log n)；雙層迴圈找配對 → 排序 + 雙指標或計數陣列；重複加總 → 前綴和）
2 資料排列：最內層循序、不複製
3 迴圈內：拿掉 printf、除法、浮點、不變運算
4 位元 builtin、查表、展開 + 多累加器
5 平行：各自累加，最後合併一次；鎖只包共享那一行
6 硬體：DMA 搬、PIO 轉換，CPU 同時算（管線）
題目像數論或圖論（取模、質數、gcd、最短路、連通、DP）→ 只回答「請貼 algorithms.md 的〈主題〉」。

【自我檢查】逐條答是或否：名稱都在清單或我貼的程式裡？DMA count 是筆數？等「收」完才讀？迴圈裡沒有 printf？加總用 int64_t？有比對原版的測試？

【RP2350 RISC-V 事實（已查證，照用）】
- 晶片 RP2350：2 核 Hazard3 RISC-V 或 2 核 Cortex-M33，開機二選一。判斷：#if defined(__riscv) 或 #if PICO_RISCV。板名字串 PICO_BOARD；flash 大小 PICO_FLASH_SIZE_BYTES（Pico 2 = 4 MB）；PICO_RP2350A=1 是 30 腳、0 是 48 腳。
- SRAM 520 KB（0x20000000 起 512 KB 交錯 + SCRATCH_X/Y 各 4 KB）。flash 經 16 KB XIP 快取。熱函式加 __not_in_flash_func(名稱)；const 表格會留在 flash，熱表格不要 const。
- Hazard3 指令（都 1 週期）：mul、mulhu；Zba sh1add/sh2add/sh3add；Zbb clz ctz cpop min max minu maxu andn orn xnor rol ror rev8 orc.b sext.b sext.h zext.h；Zbs bset bclr binv bext；Zbkb pack packh brev8 zip unzip。
- C 寫法就會產生：__builtin_popcount→cpop、__builtin_clz/ctz→clz/ctz（輸入不能是 0）、__builtin_bswap32→rev8、a&~b→andn、a<b?a:b→min、(x<<(k&31))|(x>>((32-k)&31))→rol、x/10→mulhu（不會除法）。
- 要行內組語的：brev8、orc.b、zip、unzip。位元反轉 32 位元 = brev8 + rev8。範本：uint32_t r; __asm__("brev8 %0, %1" : "=r"(r) : "r"(x));
- 沒有：浮點硬體（float 是軟體函式，熱迴圈改定點整數）、向量、SIO 除法器。div/divu 約 18 週期。clmul 不能用。
- 不對齊的 lw/sw 會當機（例外）：不要 *(uint32_t*)(buf+1)，用 memcpy。
- load 結果下一條就用多 1 週期；分支預測只記最後一個往回跳的分支，預測錯多 1 週期 → 最內層迴圈單純、展開、常見情況不跳。
- 計週期：先 __asm__ volatile("csrci mcountinhibit, 5"); 再 __asm__ volatile("csrr %0, mcycle" : "=r"(c)); （重置後計數器是停的）。長時間用 time_us_64()。
- Arm 專屬、RISC-V 不能用：CMSIS、__SMLAD 等 DSP intrinsics、SysTick、SCB、NVIC 暫存器、Thumb 組語（rbit 等）、DCP、M33 FPU。
- 兩種架構一樣的：DMA、PIO、multicore_*、spin_lock_*、critical_section_*、irq_set_exclusive_handler、time_us_64()、__wfe()/__sev()（RISC-V 上 SDK 自動換成 h3.block/h3.unblock）。
````

### A-mini（context 很緊時）

````text
你是效能工程師。板子是 RP2350B，腳位只用 board_pins.h 的 PIN_ 名稱，周邊只用 board.h 的函式。規則：只用我貼的程式與題目文件裡出現的函式名稱，不確定就說「不確定」；一次只改一處；輸出「完整新函式 + 一句理由」；保留原版，新版叫 xxx_fast；DMA count 是筆數不是位元組；RISC-V 沒有 FPU、沒有硬體除法器；先換演算法，再改資料排列與迴圈內容，再用 DMA/PIO 和雙核，全部疊上去。題目像數學或圖論（計數、取模、最短路、DP）就叫我貼 algorithms.md 的段落。
````

---

## B0. 通用「把參考解答改成這題」

````text
下面有【題目】【我目前的程式】【參考解答】。參考解答是已經編譯檢查過的正確寫法。
請照步驟：
1. 用一句話說題目要什麼、輸入多大。
2. 列出參考解答和題目「不一樣」的地方（最多 5 點：名稱、型別、大小、腳位、API）。
3. 只改那些不一樣的地方，其餘照抄參考解答。不要加新功能。
4. 輸出完整可編譯的檔案。
5. 自我檢查清單。

【題目】
{貼題目文字}

【我目前的程式】
{貼程式；沒有就寫「無」}

【參考解答】
{從 solutions.md 貼對應那一題的「最終版」整段}
````

---

## B. 針對性 prompt（每種題型一個）

每個都已經附上參考解答的**關鍵程式（節錄）**；完整可編譯版本在 [solutions.md](solutions.md) 對應的那一題，context 夠就整段貼過去取代節錄。

### B1. 搬資料 / memcpy / transfer engine → DMA（對應 S01）

````text
任務：把下面程式裡的「複製迴圈或 memcpy」改成 DMA，CPU 在搬的期間可以做別的事。
固定做法：claim 一個通道（只 claim 一次）→ default config → 設傳輸大小 → read/write 都遞增 → configure(count = 位元組數 >> 大小) → 等完成。
不准：用位元組數當 count；搬完前讀 dst；每次呼叫都 claim。
參考程式（已編譯檢查）：
```c
static int g_ch = -1;                                   // 只 claim 一次，重複用

// 啟動後立刻返回；回傳通道，之後 dma_copy_wait(ch)
int dma_copy_async(void *dst, const void *src, size_t bytes) {
    if (g_ch < 0) g_ch = dma_claim_unused_channel(true);
    uintptr_t a = (uintptr_t)dst | (uintptr_t)src | bytes;
    enum dma_channel_transfer_size sz = (a & 3) == 0 ? DMA_SIZE_32 : (a & 1) == 0 ? DMA_SIZE_16 : DMA_SIZE_8;
    uint32_t count = (uint32_t)(bytes >> sz);           // DMA_SIZE_8/16/32 = 0/1/2：筆數 = bytes >> sz
    dma_channel_config c = dma_channel_get_default_config(g_ch);
    channel_config_set_transfer_data_size(&c, sz);
    channel_config_set_read_increment(&c, true);
    channel_config_set_write_increment(&c, true);       // 沒設 dreq = 全速
    dma_channel_configure(g_ch, &c, dst, src, count, true);
    return g_ch;
}
static inline void dma_copy_wait(int ch) { dma_channel_wait_for_finish_blocking(ch); }

void dma_memcpy(void *dst, const void *src, size_t bytes) {
    if (bytes < 64) { memcpy(dst, src, bytes); return; } // 太小：設定成本比搬還貴
    dma_copy_wait(dma_copy_async(dst, src, bytes));
}

```
【題目】{貼題目}
【原程式】{貼程式}
輸出：完整新函式 + 一句理由 + 自我檢查清單。
````

### B2. 填值 / memset → DMA（對應 S02）

````text
任務：把清零或填同一個值的迴圈改成 DMA 填值。
關鍵：來源不遞增（read_increment = false）、目的遞增；填的值放 static 變數（DMA 跑完前要一直存在）。
參考程式（已編譯檢查）：
```c
static int g_ch = -1;
static uint32_t g_pattern;                               // 來源必須在 DMA 跑完前一直有效：放 static

int dma_fill32_async(uint32_t *dst, uint32_t value, uint32_t words) {
    if (g_ch < 0) g_ch = dma_claim_unused_channel(true);
    g_pattern = value;
    dma_channel_config c = dma_channel_get_default_config(g_ch);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
    channel_config_set_read_increment(&c, false);       // 來源不動 → 填值
    channel_config_set_write_increment(&c, true);
    dma_channel_configure(g_ch, &c, dst, &g_pattern, words, true);
    return g_ch;
}

// 位元組版 memset：把 byte 複製成 32 位元，對齊的部分用 32 位元填，頭尾用 CPU
void dma_memset(void *dst, int byte, size_t n) {
    uint8_t *p = dst;
    while (n && ((uintptr_t)p & 3)) { *p++ = (uint8_t)byte; n--; }      // 頭：對齊到 4
    size_t words = n >> 2;
    if (words >= 16) {
        int ch = dma_fill32_async((uint32_t *)p, 0x01010101u * (uint8_t)byte, words);
        dma_channel_wait_for_finish_blocking(ch);
    } else {
        memset(p, byte, words << 2);
    }
    p += words << 2; n &= 3;
    while (n--) *p++ = (uint8_t)byte;                                   // 尾
}
```
【題目】{貼題目}
【原程式】{貼程式}
輸出：完整新函式 + 一句理由 + 自我檢查清單。
````

### B3. CPU 一筆一筆餵 PIO → DMA 管線（對應 S03）

````text
任務：把 pio_sm_put_blocking / pio_sm_get_blocking 的迴圈改成兩個 DMA 通道：TX（記憶體→PIO）、RX（PIO→記憶體）。
固定規則：
1. RX：讀 &pio->rxf[sm] 不遞增、寫 dst 遞增、dreq = pio_get_dreq(pio, sm, false)。
2. TX：讀 src 遞增、寫 &pio->txf[sm] 不遞增、dreq = pio_get_dreq(pio, sm, true)。
3. 先啟動 RX，再啟動 TX。
4. 等 RX 完成（不是 TX）才讀結果。
參考程式（已編譯檢查）：
```c
// 回傳 rx 通道；呼叫者 dma_channel_wait_for_finish_blocking(rx)
static int pipe_start(PIO pio, uint sm, const uint32_t *src, uint32_t *dst, uint32_t words, int *tx_out) {
    int tx = dma_claim_unused_channel(true);
    int rx = dma_claim_unused_channel(true);

    dma_channel_config c = dma_channel_get_default_config(rx);       // PIO RX FIFO → dst
    channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
    channel_config_set_read_increment(&c, false);
    channel_config_set_write_increment(&c, true);
    channel_config_set_dreq(&c, pio_get_dreq(pio, sm, false));
    dma_channel_configure(rx, &c, dst, &pio->rxf[sm], words, true);  // 先啟動接收端

    c = dma_channel_get_default_config(tx);                           // src → PIO TX FIFO
    channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
    channel_config_set_read_increment(&c, true);
    channel_config_set_write_increment(&c, false);
    channel_config_set_dreq(&c, pio_get_dreq(pio, sm, true));
    dma_channel_configure(tx, &c, &pio->txf[sm], src, words, true);
    *tx_out = tx;
    return rx;
}
```
PIO 端若用 autopull/autopush：sm_config_set_out_shift(&c, true, true, 32); sm_config_set_in_shift(&c, false, true, 32);
【題目】{貼題目}
【PIO 程式】{貼 .pio}
【原 C 程式】{貼程式}
輸出：完整新 C 檔 + 一句理由 + 自我檢查清單。
````

### B4. 看懂 / 改寫 PIO 程式（對應 S04）

````text
任務：{看懂這支 PIO 程式 / 讓它更快 / 改成做 X}。
PIO 只有：X、Y 兩個暫存器，OSR（out 取出）、ISR（in 放入），指令 jmp wait in out push pull mov irq set nop。沒有加法，只有 jmp x-- / y-- 遞減。
mov 的 ~ 是反相、:: 是位元反轉。.wrap_target / .wrap 迴圈不花指令。最多 32 條指令。
已組譯過的例子：
  原樣送回（autopull+autopush）：out x, 32 / in x, 32
  反相：pull block / mov isr, ~osr / push block
  位元反轉：pull block / mov isr, ::osr / push block
優化順序：1 改 autopull/autopush 省 pull/push  2 用 .wrap 省 jmp  3 單向時 fifo_join  4 clkdiv 1.0  5 side_set 省 set  6 一次 out 多位元。
步驟：1 逐行解釋原程式每條指令做什麼  2 指出每個 word 花幾條指令  3 給出改寫版  4 說明 C 端要改哪些 sm_config 設定。
【PIO 程式】{貼 .pio}
【C 端設定】{貼 init 函式}
````

### B5. CRC / checksum（對應 S05）

````text
任務：加速 CRC 或校驗和。
選擇：只要軟體 → 查表法（每 byte 一次查表）；要搬資料順便算 → DMA sniffer。
參考程式（查表版在 Linux 上與 zlib 比對過；"123456789" 的 CRC-32 = cbf43926）：
```c
static uint32_t T[256];
void crc32_init(void) {
    for (uint32_t i = 0; i < 256; i++) { uint32_t c = i; for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & -(c & 1)); T[i] = c; }
}
uint32_t crc32_table(const uint8_t *p, size_t n) {
    uint32_t c = 0xFFFFFFFFu;
    while (n--) c = T[(c ^ *p++) & 0xFF] ^ (c >> 8);
    return ~c;
}
// ---- 硬體：DMA sniffer（CRC32R = 反射版多項式） ----
uint32_t crc32_dma(const uint8_t *p, size_t n) {
    static uint8_t sink;
    int ch = dma_claim_unused_channel(true);
    dma_channel_config c = dma_channel_get_default_config(ch);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_8);
    channel_config_set_read_increment(&c, true);
    channel_config_set_write_increment(&c, false);
    channel_config_set_sniff_enable(&c, true);
    dma_sniffer_set_data_accumulator(0xFFFFFFFFu);
    dma_sniffer_set_output_reverse_enabled(true);     // 同 pico-examples dma/sniff_crc
    dma_sniffer_set_output_invert_enabled(true);      // 最後取反 = 標準 CRC-32 的 ~c（依 pico-examples 推導，上機仍先比對軟體版）
    dma_sniffer_enable(ch, DMA_SNIFF_CTRL_CALC_VALUE_CRC32R, true);
    dma_channel_configure(ch, &c, &sink, p, n, true);
    dma_channel_wait_for_finish_blocking(ch);
    uint32_t r = dma_sniffer_get_data_accumulator();
    dma_sniffer_disable();
    dma_channel_unclaim(ch);
    return r;
}
// 簡單加總校驗：DMA_SNIFF_CTRL_CALC_VALUE_SUM，32 位元傳輸時一次加一個 word
```
注意：DMA 版設定照 pico-examples（CRC32R、種子 0xFFFFFFFF、output_reverse）再加 output_invert，推導上等於標準 CRC-32，但沒上板；程式要同時印出查表版與 DMA 版比對。
【題目】{貼題目，特別是多項式、初值、是否反射、最後是否取反}
【原程式】{貼程式}
````

### B6. 大陣列切兩顆核心（對應 S06）

````text
任務：把一個大迴圈切成兩半，core0 和 core1 同時算。
規則：multicore_launch_core1 只呼叫一次；FIFO 一格 32 位元，只傳工作描述的指標；core1 算完 push 一個完成訊號；core0 算自己那半後 pop 等 core1；兩核寫不同變數，最後相加。
參考程式（已編譯檢查）：
```c
typedef struct { const int32_t *p; uint32_t n; int64_t result; } job_t;
static job_t job1;                                    // core1 的工作描述

static int64_t sum_range(const int32_t *p, uint32_t n) {
    int64_t s0 = 0, s1 = 0;                           // 兩個累加器打斷相依鏈
    uint32_t i = 0;
    for (; i + 2 <= n; i += 2) { s0 += p[i]; s1 += p[i + 1]; }
    if (i < n) s0 += p[i];
    return s0 + s1;
}
static void core1_entry(void) {
    while (true) {
        job_t *j = (job_t *)multicore_fifo_pop_blocking();   // FIFO 只傳指標（32 位元）
        j->result = sum_range(j->p, j->n);
        multicore_fifo_push_blocking(1);                     // 完成訊號
    }
}
int64_t sum_dual(const int32_t *p, uint32_t n) {
    uint32_t h = n / 2;
    job1.p = p + h; job1.n = n - h;
    multicore_fifo_push_blocking((uint32_t)(uintptr_t)&job1);
    int64_t s = sum_range(p, h);                             // core0 同時做前半
    multicore_fifo_pop_blocking();                           // 等 core1
    return s + job1.result;
}

```
【題目】{貼題目}
【原程式】{貼迴圈}
輸出：完整程式（含 core1_entry 與 main 的啟動）+ 自我檢查清單。
````

### B7. 兩核心或多執行緒共用變數 / 鎖太慢（對應 S07、S14）

````text
任務：共用計數器或共用變數的程式，要正確且快。
選擇順序（越前面越快）：
1. 各自用區域變數累加，最後只鎖一次合併。
2. 只有一個計數器 → atomic_fetch_add_explicit(&x, 1, memory_order_relaxed)。
3. 一定要鎖 → 鎖只包「讀改寫共享變數」那一行，運算放鎖外。
RP2350：uint32_t save = spin_lock_blocking(lk); ... spin_unlock(lk, save);
Linux：pthread_spin_lock(&lock); ... pthread_spin_unlock(&lock);
參考程式（Linux 實測：長鎖 87 ms → 短鎖 43 ms → 本地累加 34 ms）：
```c
static void *w_atomic(void *a) { (void)a; for (int i = 0; i < ROUNDS; i++) { sink += work(i); atomic_fetch_add_explicit(&acounter, 1, memory_order_relaxed); } return 0; }
// 最好：各自累加，最後鎖一次（或 atomic 一次）
static void *w_local(void *a) { (void)a; long n = 0; uint32_t s = 0;
    for (int i = 0; i < ROUNDS; i++) { s += work(i); n++; }
    pthread_spin_lock(&lock); counter += n; pthread_spin_unlock(&lock); sink += s; return 0; }

```
【題目】{貼題目}
【原程式】{貼程式}
輸出：完整新函式 + 一句理由 + 自我檢查清單（加一條：每次跑結果都一樣嗎？）。
````

### B8. 生產者 / 消費者佇列（對應 S08）

````text
任務：把有鎖的佇列換成單生產者單消費者（SPSC）無鎖環形佇列。
條件：只有一個寫入者、一個讀取者；容量是 2 的次方；寫資料後用 release 更新 head；讀之前用 acquire 讀 head。多個生產者時不能用這個，改用 pico/util/queue.h。
參考程式（Linux ThreadSanitizer 驗證無資料競爭）：
```c
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
```
【題目】{貼題目}
【原程式】{貼程式}
````

### B9. 位元運算（對應 S09）

````text
任務：加速或實作位元運算。
直接用（已在 RP2350 RISC-V 編譯確認成單一或少數指令）：
  1 的個數 __builtin_popcount(x) → cpop
  奇偶 __builtin_popcount(x) & 1
  floor(log2 x) 31 - __builtin_clz(x)（x 不能是 0）
  最低位的 1 x & -x；清掉最低位的 1 x & (x - 1)；是否 2 的次方 x && !(x & (x - 1))
  取 bits[hi:lo] (x >> lo) & ((2u << (hi - lo)) - 1)
  位元組反轉 __builtin_bswap32(x)；循環左移 (x << k) | (x >> ((32 - k) & 31))
  只走訪 1 的位元 while (m) { i = __builtin_ctz(m); ...; m &= m - 1; }
規則：一律 uint32_t；位移量 < 32；不要逐位元 for 32 次。
【題目】{貼題目}
【原程式】{貼程式}
輸出：完整新函式 + 用 10 組輸入（含 0、1、0xFFFFFFFF、0x80000000）比對原版的測試。
````

### B10. 查表 vs 現算（對應 S10）

````text
任務：判斷這個函式該不該改成查表，並實作。
規則：輸入範圍 ≤ 65536 而且函式比一次記憶體讀取貴（sin、CRC、不規則映射）→ 開機時建表；CPU 有單一指令能做的（位元反轉、popcount）→ 不要查表。表不要宣告成 const（會留在 flash 較慢），開機時填。RISC-V 沒有 FPU：sin 用 Q15 整數表 + 線性內插。
【題目】{貼題目}
【原函式】{貼函式}
輸出：1 該不該查表（是/否 + 一句理由） 2 若是：建表函式 + 查表函式 + 比對原版的測試。
````

### B11. 排序 / 搜尋（對應 S11）

````text
任務：選最快的排序或搜尋方法。
選擇表（Linux 實測：100 萬個 uint32 qsort 113 ms → 基數排序 16 ms；100 萬個 byte qsort 121 ms → 計數排序 0.8 ms）：
  n ≤ 32 → 插入排序
  值域小（byte、0..999）→ 計數排序
  32 位元整數、n 大、有同大小暫存 → 基數排序（4 輪 × 8 位元）
  只要第 k 小 / 中位數 → quickselect
  已排序要查很多次 → 二分（lower_bound）
步驟：1 寫出 n 的上限與值域  2 依表選方法  3 寫完整函式  4 用 1000 組隨機輸入對照 qsort 的測試。
比較函式要寫 (a > b) - (a < b)，不要寫 a - b（會溢位）。
【題目】{貼題目}
【原程式】{貼程式}
````

### B13. pthread 平行加總 / 最大值 / 計數（對應 S13）

````text
任務：用 pthread 把一個大迴圈平行化。
規則：執行緒數 = sysconf(_SC_NPROCESSORS_ONLN)；每個執行緒拿 [lo, hi) 一段；用區域變數累加，最後寫一次到自己的結果欄位；結果欄位用 _Alignas(64) 隔開；主執行緒 join 後相加。不要每個元素都上鎖。
參考程式（Linux 4 核實測約 3–4 倍）：
```c
typedef struct { _Alignas(64) int64_t sum; int lo, hi; } part_t;   // 每個執行緒一條快取行，避免偽共享
static part_t part[MAXT];

static void *worker(void *arg) {
    part_t *p = arg;
    int64_t s = 0;                                  // 區域變數累加，不要直接 p->sum += 
    for (int i = p->lo; i < p->hi; i++) s += a[i];
    p->sum = s;                                     // 最後寫一次
    return NULL;
}
static int64_t sum_parallel(int nt) {
    pthread_t th[MAXT];
    for (int t = 0; t < nt; t++) {
        part[t].lo = (int)((int64_t)N * t / nt); part[t].hi = (int)((int64_t)N * (t + 1) / nt);
        pthread_create(&th[t], NULL, worker, &part[t]);
    }
    int64_t s = 0;
    for (int t = 0; t < nt; t++) { pthread_join(th[t], NULL); s += part[t].sum; }
    return s;
}
```
【題目】{貼題目}
【原程式】{貼迴圈}
輸出：完整程式 + 比對單執行緒結果的測試。編譯：gcc -O2 -pthread。
````

### B14. 矩陣 / 2D 陣列（對應 S15）

````text
任務：加速多層迴圈的 2D 陣列運算。
規則：讓最內層迴圈的索引對應記憶體連續的那一維（row-major 就是最後一個索引）。矩陣乘法 i-j-k 改 i-k-j，先把 C 清零，把 A[i][k] 提到最內層外面，指標加 restrict。
參考（Linux 512×512 實測 115 ms → 23 ms）：
```c
// 優化 1：換成 i-k-j，最內層 B、C 都連續存取（編譯器還能向量化）
static inline void mm_ikj(int n, const int32_t *restrict A, const int32_t *restrict B, int32_t *restrict C) {
    memset(C, 0, sizeof(int32_t) * n * n);
    for (int i = 0; i < n; i++)
        for (int k = 0; k < n; k++) { int32_t a = A[i*n+k]; const int32_t *b = &B[k*n]; int32_t *c = &C[i*n];
            for (int j = 0; j < n; j++) c[j] += a * b[j]; }
}
```
【題目】{貼題目}
【原程式】{貼迴圈}
輸出：完整新函式 + 用小矩陣（例如 7×7）比對原版的測試。
````

### B15. RISC-V 組合語言（對應 S16）

````text
任務：寫或優化 RV32 組合語言（RP2350 Hazard3：rv32imac + Zba/Zbb/Zbs/Zbkb）。
呼叫慣例：參數 a0–a7，回傳 a0；t0–t6 隨便用；s0–s11 用了要先存後還；回傳用 ret。
可用的加速指令：sh1add/sh2add/sh3add（位址 = 基底 + 索引×2/4/8）、cpop、clz、ctz、min、max、minu、maxu、andn、orn、xnor、rol、ror、rev8、brev8、bset、bclr、binv、bext。
技巧：4 路展開；兩個累加器；用結尾位址比較取代計數器；load 的結果不要下一條就用。
區段：.section .time_critical.名稱, "ax" 才會放 RAM。
參考（模擬器驗證正確，n=1000 指令數約為逐筆迴圈的一半）：
```asm
    .section .time_critical.asm_sum4, "ax"     # 放 SRAM（.time_critical.* 由 SDK linker script 放 RAM）
    .globl asm_sum4
    .type  asm_sum4, @function
asm_sum4:
    sh2add  a2, a1, a0          # a2 = a + n*4（結尾位址；Zba，一條取代 slli+add）
    andi    a3, a1, -4          # a3 = n & ~3
    sh2add  a3, a3, a0          # a3 = 4 的倍數部分的結尾
    li      t0, 0
    li      t1, 0               # 兩個累加器，打斷相依鏈
    beq     a0, a3, 2f
1:  lw      t2, 0(a0)
    lw      t3, 4(a0)
    lw      t4, 8(a0)
    lw      t5, 12(a0)
    add     t0, t0, t2
    add     t1, t1, t3
    add     t0, t0, t4
    add     t1, t1, t5
    addi    a0, a0, 16
    bne     a0, a3, 1b          # 用位址比較，省掉計數器
2:  beq     a0, a2, 4f          # 剩 0~3 個
3:  lw      t2, 0(a0)
    add     t0, t0, t2
    addi    a0, a0, 4
    bne     a0, a2, 3b
4:  add     a0, t0, t1
    ret
    .size asm_sum4, .-asm_sum4
```
【題目】{貼題目}
【原程式】{貼 C 或組語}
輸出：完整 .S 檔 + C 端宣告 + 一句理由。
````

### B16. 全層疊加：DMA + PIO + 雙核同時跑（對應 S17）

````text
任務：把「CPU 一個字一個字 put/get PIO 再計算」改成管線，讓 DMA、PIO、core0、core1 同時工作。
固定結構（照抄，只改 work()、PIO 程式名稱、N）：
1. TX DMA 一次把整個輸入送進 PIO。
2. RX 用兩個 DMA 通道，每塊 BLK 個字；第 k 塊收完的 IRQ 裡：啟動另一個通道收第 k+1 塊，把自己的寫入位址設到第 k+2 塊，blocks_ready = k + 1。
3. core0 處理偶數塊、core1 處理奇數塊；各自等 blocks_ready > k，加 __dmb() 再讀。
4. 先啟動 RX 再啟動 TX；處理函式與 IRQ 用 __not_in_flash_func。
參考程式的關鍵部分（已編譯檢查；排程邏輯用模型驗證過）：
```c
// 第 k 塊收完：立刻啟動另一個通道收第 k+1 塊，再把這個通道指到第 k+2 塊
static void __not_in_flash_func(dma_irq)(void) {
    for (int j = 0; j < 2; j++) {
        int ch = rx_ch[j];
        if (!dma_channel_get_irq0_status(ch)) continue;
        dma_channel_acknowledge_irq0(ch);
        uint32_t k = blocks_ready;
        if (k + 1 < NBLK) dma_channel_start(rx_ch[j ^ 1]);      // 長度會自動重新載入
        if (k + 2 < NBLK) dma_channel_set_write_addr(ch, &out[(k + 2) * BLK], false);
        blocks_ready = k + 1;
    }
}

static void core1_entry(void) {
    uint64_t s = 0;
    for (uint32_t k = 1; k < NBLK; k += 2) {                  // core1：奇數塊
        while (blocks_ready <= k) tight_loop_contents();
        __dmb();
        s += process_block(&out[k * BLK]);
    }
    core1_sum = s;
    multicore_fifo_push_blocking(1);                           // 通知 core0 做完了
}

```
【題目】{貼題目}
【原程式】{貼程式}
輸出：完整檔案 + 一句理由 + 自我檢查清單（加兩條：N 是 BLK 的倍數嗎？multicore_launch_core1 只呼叫一次嗎？）。
````

### B17. 先換演算法（對應 S18）

````text
任務：判斷這個程式能不能換成複雜度更低的演算法。這一層常常是 100 倍以上，比任何硬體加速都大。
步驟：
1. 寫出原程式的複雜度和 n 的上限，算出總運算次數。
2. 對照下表找出符合的模式：
   雙層迴圈找 a[i]+a[j]=K → 排序 + 雙指標；值域小用計數陣列
   每次查詢重加一段 → 前綴和 pre[r+1] - pre[l]
   反覆線性搜尋 → 排序一次 + 二分
   判斷是否出現過 → 值域小用 bitset / 陣列，否則雜湊
   重複算同一個子問題 → 記憶化 / DP
   取模的次方 → 快速冪
   最短路、連通、計數、質數、gcd → 叫我貼 algorithms.md 的段落
3. 寫出新版完整函式（名稱 xxx_fast）。
4. 寫一個用隨機小輸入比對原版的測試（n 從 0 到 50 各測幾次）。
實測參考（Linux）：數對計數 n=40000 雙層迴圈 338 ms → 計數陣列 0.15 ms；區間和 20000 次查詢 344 ms → 前綴和 0.2 ms。
【題目】{貼題目，含 n 和值域}
【原程式】{貼程式}
````

---

### B18. 板上周邊：LED、蜂鳴器、按鍵、馬達、編碼器、伺服、I2C、LCD、UART（對應 board.md）

先貼 A，再貼這段，最後貼你的 `board_pins.h`（填好腳位的版本，約 600 token）。不用貼 board.h 全文，下面已列出它的函式。

````text
任務：用板上周邊完成題目要求。板子是 RP2350B 機器人控制板。
【可用函式（board.h，已寫好，直接呼叫，不准重寫、不准改名）】
leds_init(); leds_set(i, r, g, b); leds_show();          // 6 顆 WS2812，i = 0..5，設完要 leds_show 才會更新
buzzer_init(); buzzer_tone(hz); buzzer_beep(hz, ms);      // hz = 0 靜音；buzzer_beep 會阻塞 ms
keys_init(); key_raw(i); key_pressed_edge(i);             // i = 0 是 KEY1、1 是 KEY2；key_pressed_edge 每 1 ms 呼叫一次，剛按下回 true
motors_init(); motor_set(m, speed); motor_brake(m);       // m = 0..3，speed = -1000..1000
encoders_init(); encoder_get(i);                          // i = 0..3，回傳 int32_t 累計計數
servo_init(pin); servo_deg(pin, deg); servo_us(pin, us);  // pin 用 PIN_SERVO1 等
i2c_bus_init(i2c0, PIN_I2C0_SDA, PIN_I2C0_SCL, 400000); i2c_scan(i2c0);
i2c_reg_read(i2c0, addr, reg, buf, n); i2c_reg_write(i2c0, addr, reg, val);   // 回傳 < 0 表示失敗
lcd_init(); lcd_fill(x, y, w, h, RGB565(r, g, b));
uart_link_init(uart1, PIN_UART1_TX, PIN_UART1_RX, 115200); uart_read_line(u, buf, cap, &len);  // 非阻塞，整行回傳長度否則 -1
vbat_init(); vbat_read();                                  // 回傳 float 伏特
【規則】
1. 腳位只寫 PIN_ 名稱。PWM 同一個 slice（相鄰兩支腳 2k、2k+1）頻率共用：伺服(50 Hz)和馬達(20 kHz)不能放同一個 slice。
2. 主迴圈不准長時間 sleep_ms；要同時顧多件事就用 absolute_time_t t = make_timeout_time_ms(n); if (time_reached(t)) {...}。
3. 固定週期控制（例如每 10 ms 算一次速度）用 add_repeating_timer_ms(-10, cb, NULL, &timer)；callback 內不准 printf、不准 sleep，回傳 true。
4. 速度 = 每週期編碼器計數差值。PI 控制：out = Kp*err + Ki*積分，積分要限幅。
5. 中斷和主迴圈共用的變數加 volatile。
【輸出】(1) 完整 main.c（#include "board_pins.h" 再 #include "board.h"）(2) CMakeLists 需要加的連結庫（若有）(3) 怎麼在板上驗證，三步以內 (4) 自我檢查：沒寫死腳位數字？沒在 callback 裡 printf？leds_set 後有 leds_show？
【題目】{貼題目}
【board_pins.h】{貼}
````

### B19. 文件或原理圖 → board_pins.h

````text
下面是考場提供的板子文件（或原理圖的文字、範例程式）。請只做一件事：把每個周邊用到的 GPIO 號碼找出來，填進這個表，找不到的寫「找不到」，不准猜。
周邊清單：WS2812 資料腳、蜂鳴器、KEY1、KEY2、M1–M4 各兩支控制腳（IN1/IN2 或 PWM/DIR，寫明是哪種）、編碼器 1–4 的 A/B、伺服或 IO 排針、I2C0 SDA/SCL、I2C1 SDA/SCL、UART0 TX/RX、無線模組的介面與腳位、LCD 的 SCK/MOSI/CS/DC/RST/BL、電池電壓 ADC 腳與分壓電阻。
輸出：(1) 表格：周邊 | GPIO | 文件原文出處（一句）(2) 照下面格式的 #define 清單，可以直接貼進 board_pins.h：#define PIN_XXX n  // 出處
規則：GPIO 號碼範圍 0–47；ADC 只能在 40–47；一樣的號碼出現兩次就標「衝突」。
【文件】{貼}
````

---

## C. 編譯錯誤修正

````text
下面是編譯錯誤和程式。請照步驟：
1. 只看「第一個」error（後面的常是連鎖錯誤）。
2. 說出它是哪一類：缺 #include / 名稱打錯或不存在 / 型別不合 / 缺 CMake 連結庫 / 語法。
3. 只修這一個錯，輸出修改的那幾行（前後各留 2 行原文方便我找位置）。
4. 若錯誤是「undefined reference」或「implicit declaration」：先懷疑函式名稱是捏造的。只能換成已查證 API 或程式中已有的名稱；不知道正確名稱就說「不確定，請查 cheatsheet.md」。
5. 常見對應：hardware/dma.h → target_link_libraries 加 hardware_dma；hardware/pio.h → hardware_pio；pico/multicore.h → pico_multicore；hardware/sync.h → hardware_sync；printf → #include <stdio.h>。
【錯誤訊息】{貼前 30 行}
【相關程式】{貼出錯的檔案或函式}
【CMakeLists.txt】{貼 target_link_libraries 那幾行}
````

---

## D. 找瓶頸（照清單走，不自由發揮）

````text
請「逐條」檢查下面程式，每條回答 是 / 否 / 不適用，並指出行號。不要給清單外的建議。
1. 迴圈裡有 printf / print / log / sleep？
2. 演算法複雜度是多少？輸入上限 {n} 下需要多少次運算？
3. 有兩層以上迴圈在找東西或配對（可用排序+二分或雜湊）？
4. 迴圈裡有除法或取模？除數是常數、2 的次方、還是變數？
5. 迴圈裡有浮點運算？（RISC-V 沒有 FPU）
6. 迴圈裡有每次都一樣的運算可以提出去？
7. 最內層迴圈的陣列存取是連續的嗎？
8. 有逐位元的 for 迴圈可以換成 __builtin_popcount / clz / ctz？
9. 有複製或填值的迴圈可以換 DMA？
10. 有一筆一筆 put/get PIO 可以換 DMA 管線？
11. 有鎖？鎖裡面有不必要的運算？
12. 有可以切成兩半平行的獨立迴圈？
最後：依「預估節省時間」排序，列出前 3 個要改的地方（只列，不要寫程式）。
【輸入範圍與時間限制】{貼}
【程式】{貼}
````

---

## F. 產生對拍測試（確認優化沒改壞）

````text
我有兩個函式：原版 {name} 和優化版 {name}_fast，簽名相同：{貼簽名}。
請寫一個 main：
1. 用固定種子的 xorshift 產生隨機輸入（不要用 rand()）。
2. 先測邊界：長度 0、1、2、3、不是 4 的倍數、最大長度；數值 0、1、-1、最大值、最小值、全部相同、已排序、反向排序。
3. 再測 10000 組隨機輸入。
4. 每組比對兩個輸出，第一個不同時印出輸入、兩個輸出並結束。
5. 全部相同印 "ALL OK"。
只用 <stdio.h> <stdint.h> <string.h>。
````

---

## G. 數論 / 圖論題

````text
這題看起來是 {計數 / 取模 / 質數 / gcd / 最短路 / 連通 / 拓撲排序 / DP}。
下面是參考資料（從 algorithms.md 貼來），只能用參考資料裡的方法與公式，不要自己推新公式：
【參考資料】{貼 algorithms.md 的對應段落}
【題目】{貼題目}
請照步驟：1 題目對應參考資料的哪個方法  2 複雜度是多少，是否在時間限制內  3 寫完整程式  4 用題目範例手算驗證一次。
````

---

## H. 小模型卡住或答錯時

### H1. 修正迴圈（一次只修一個錯）

每次失敗就重貼這則，換掉【結果】。同一個錯修 3 次還不行，就開新對話，從 A + 對應的 B 重來。

````text
你上一版程式失敗了。下面是【結果】。
規則：
1. 只看結果裡「第一個」錯誤。
2. 用一句話說原因，分類：編譯錯誤 / 執行當掉 / 答案錯 / 太慢。
3. 只改造成這個錯的地方，其餘一個字都不准動。
4. 輸出：被改的函式完整版（不是片段），以及改了哪幾行。
5. 若原因是你用了不存在的函式名稱，換成我貼的程式或 A 清單裡的名稱；找不到就回答「不確定，請查 cheatsheet.md」。
6. 答案錯：先列出一組你認為會錯的最小輸入，手算正確答案，再改。
7. 太慢：先說目前複雜度，再照 A 的【優化】清單選下一層。
【結果】{貼編譯錯誤前 30 行 / 當機時印出的最後幾行 / 評測回傳的訊息}
【目前程式】{貼出錯的函式}
````

### H2. 五行看懂考場給的程式

````text
用 5 行以內說明下面這段程式，每行一件事，照這個順序：
1. 輸入是什麼、輸出是什麼
2. 主要迴圈或流程在做什麼
3. 用到哪些硬體或 API（DMA、PIO、多核、鎖、PWM、I2C、SPI、UART、ADC）
4. 最花時間的是哪一行（寫行號）和原因
5. 有沒有看起來故意放的錯（count 單位、沒等完成、鎖範圍、溢位）
不要改程式，不要給建議，不要超過 5 行。
【程式】{貼程式}
````

### H3. 文件段落 → 暫存器 / API 表

考試考「讀文件掌握軟硬體架構」。把題目給的規格或 datasheet 段落轉成表，再拿表去寫程式。

````text
把下面的文件段落整理成表格，只用段落裡寫的內容，沒寫的格子填「文件沒寫」，不准自己補。
表 1：暫存器或 API
| 名稱 | 位址或函式簽名 | 欄位 / 參數 | 位元範圍 | 讀寫 | 意義 | 單位 |
表 2：使用順序（編號步驟：設定 → 啟動 → 等待 → 取結果）
表 3：限制與陷阱（對齊、單位是筆數還是位元組、最大值、要先清除的旗標、要等的狀態位元）
最後一行：這段文件最容易被誤用的一點。
【文件段落】{貼段落}
````

拿到表之後，下一則再貼：「依照表 1 和表 2，用 C 寫出 {要做的事}，只能用表裡的名稱。」
