# 預測考題 × 完整優化解答（上機考用）

> 依練習材料（`demo_2`：ROS 2 topic 管線、bitwise 與 PIO echo、spinlock 臨界區、transfer engine）和考試公告（讀文件掌握軟硬體架構、優化程式與整體系統效能、Online Judge 計分）推測的題型。**每題都能單獨複製貼給小模型**，不必連整份一起貼。
>
> **查證方式**：RP2350 程式用 pico-sdk 2.3.1 標頭以 clang 對 `rp2350-riscv` 與 Arm Cortex-M33 兩種目標做過語法檢查（沒有開發板，**沒在實機上跑**）；`.pio` 用 SDK 的 pioasm 組譯過；能在一般電腦上跑的（位元運算、查表、排序、CRC、SPSC、pthread、矩陣）都用隨機資料比對過「優化前後結果完全相同」並實測時間（4 核 Linux VM，GCC 13）；手寫 RISC-V 組合語言用自寫的指令模擬器比對過結果；ROS 2 名稱對照 Jazzy 原始碼，Python 版過了 `py_compile`，C++ 版沒有編譯。標「估計」的加速比是推算，不是量出來的。
>
> 相關檔案：[00-START.md](00-START.md)（考試當天入口）、[build-and-debug.md](build-and-debug.md)（建置與除錯）、[cheatsheet.md](cheatsheet.md)（API 速查）、[optimization.md](optimization.md)（優化技巧）、[methodology.md](methodology.md)（卡住時照著走）、[prompts.md](prompts.md)（貼給小模型的 prompt）、[algorithms.md](algorithms.md)（數論、圖論）。

## 速查：題目長這樣 → 跳到哪

| 題目裡出現… | 跳到 |
|---|---|
| 複製陣列、`memcpy`、搬資料、「不要用 CPU 迴圈」、transfer engine | [S01](#s01) |
| 填值、清零、`memset`、整塊設成同一個值 | [S02](#s02) |
| CPU 把資料餵給 PIO 再讀回、資料串流經過 PIO、echo | [S03](#s03) |
| PIO 程式要做反相 / 位元反轉 / 計數 / 改格式 | [S04](#s04) |
| checksum、CRC、驗證資料完整性 | [S05](#s05) |
| 大陣列運算、加總、「兩顆核心」、`multicore` | [S06](#s06) |
| 兩顆核心共用計數器、共用變數、spin lock、critical section（板子上） | [S07](#s07) |
| 一邊產生資料一邊處理、生產者/消費者、佇列、ring buffer | [S08](#s08) |
| popcount、parity、bit reverse、取位元欄位、`clz`/`ctz`、RISC-V bit 指令 | [S09](#s09) |
| 重複算同一個函式、sin/cos、byte 轉換表、查表 | [S10](#s10) |
| 排序、找第 k 小、搜尋、中位數 | [S11](#s11) |
| ROS 2 node、topic、延遲、`/number` → `/double` | [S12](#s12) |
| pthread 平行加總 / 最大值 / 計數 | [S13](#s13) |
| pthread 計數器少算、lock 太慢（練習題 critical-section） | [S14](#s14) |
| 矩陣乘法、2D 陣列、影像、迴圈很多層 | [S15](#s15) |
| 要寫或優化 RISC-V 組合語言 | [S16](#s16) |
| 資料經過 PIO/周邊再由 CPU 算、「整體系統效能」、要 DMA 和雙核一起用 | [S17](#s17) |
| 雙層迴圈找配對、反覆加總同一段、n 很大 | [S18](#s18) |
| 計數、取模、質數、gcd、路徑、連通、DP | [algorithms.md](algorithms.md) |

**每題共通的最後一步**：保留原版函式，新版取名 `xxx_fast`，同一筆輸入兩邊都跑，結果完全一樣才交。量時間用 `time_us_64()`（板子）或 `clock_gettime(CLOCK_MONOTONIC)`（Linux），跑多次取最小值。

## 輾壓原則：每題都要疊滿這五層

小優化只會贏 10–30%。要拉開差距，**每一層都疊上去**，而且順序不能反：

1. **演算法**：換掉複雜度。這層常常是 100 倍以上（見 [S18](#s18) 實測：數對計數 338 ms → 0.15 ms，區間和 344 ms → 0.2 ms）。
2. **資料排列**：最內層循序存取、每個字用滿（32 位元傳輸、bit-packing）、不搬資料（直接在 DMA 的目的地處理，零複製）。
3. **RAM 與編譯器**：熱函式 `__not_in_flash_func`、表格放 SRAM（不要 `const`）、`-O3`（SDK 預設就是）、`restrict`、拿掉迴圈裡的除法與浮點。
4. **硬體卸載**：搬資料給 DMA、逐位元或格式轉換給 PIO、checksum 給 DMA sniffer，CPU 只做真正的運算。
5. **平行**：兩顆核心都在算（板子）、每核一段（pthread），**加上** DMA/PIO 同時在搬 → 四件事重疊，總時間 ≈ 最慢那一件，而不是全部相加。

**結構性重寫優先於微調**：先把「搬 → 變換 → 算」從排隊改成管線（[S17](#s17)），再去摳單一迴圈。

### 每題的疊加配方與總加速

「實測」= 4 核 Linux VM、GCC 13、和原版比對結果完全相同；「估計」= 沒有開發板，推算值，**考場要自己量**。

| 題 | 1 演算法 | 2 排列 | 3 RAM / 編譯 | 4 DMA / PIO | 5 雙核 / 執行緒 | 總加速 |
|---|---|---|---|---|---|---|
| [S01](#s01) 複製 | — | 32 位元傳輸、對齊 | 搬的時候 CPU 去算 | DMA，乒乓兩塊 | core1 算已搬完的那塊 | 估計：總時間從「搬 + 算」降到「max(搬, 算) ÷ 2 核」 |
| [S02](#s02) 填值 | — | 32 位元 | — | DMA 填值 | 填的同時兩核做別的 | 估計 2–4 倍 |
| [S03](#s03) PIO 管線 | — | 整塊傳 | 處理函式放 RAM | TX/RX DMA + 乒乓 | 兩核分奇偶塊 | **用 [S17](#s17)**；估計 3–6 倍 |
| [S04](#s04) PIO 程式 | 減指令數 | autopull/autopush、`fifo_join` | clkdiv 1.0 | DMA 餵 FIFO | 資料切 2–4 份給多個 SM，每個 SM 一對 DMA | 估計 2–4 倍 × SM 數 |
| [S05](#s05) CRC | 逐位元 → 查表 | — | 表放 RAM | 搬資料時 sniffer 順便算 | — | 實測查表 4.2 倍；sniffer 估計 CPU 時間趨近 0 |
| [S06](#s06) 雙核加總 | 先看能不能用公式或前綴和（[S18](#s18)） | 連續存取 | 放 RAM、兩個累加器 | 資料由 DMA 送進來 | 雙核 | 估計 1.8–2 倍 × 前面幾層 |
| [S07](#s07) 共用計數 | 本地累加，最後合併一次 | 結果分開放 | — | — | 雙核 | 實測（Linux 類比）長鎖 87 → 本地 34 ms，2.6 倍；板子估計更多 |
| [S08](#s08) 佇列 | 無鎖 SPSC | 容量 2 的次方、批次取 | 放 RAM | — | 一核生產一核消費 | 實測 5,000 萬筆 1.27 s |
| [S09](#s09) 位元 | 逐位元 → SWAR / builtin | — | RAM | — | 執行緒 / 雙核 | 實測 353 → 10–18 ms，**20–37 倍** |
| [S10](#s10) 查表 | 能算的就算 | 表放 RAM | — | — | — | 實測依函式而定（反轉位元現算較快） |
| [S11](#s11) 排序 | qsort → 基數 / 計數排序 | — | — | — | — | 實測 7 倍（uint32）、150 倍（byte） |
| [S12](#s12) ROS 2 | 拿掉 print、重用訊息 | — | 改 C++ | — | MultiThreadedExecutor | 估計，依題目量 |
| [S13](#s13) pthread 加總 | 能用公式就不用迴圈 | 連續存取、結果 64 位元組對齊 | `-O3` | — | 每核一段 | 實測 3–4 倍 |
| [S14](#s14) 臨界區 | 本地累加 | — | — | — | 只鎖合併那一行 | 實測 2.6 倍 |
| [S15](#s15) 矩陣 | i-j-k → i-k-j | `restrict` | `-O3` | — | 每核分列 | 實測 34–54 → 5.7–10 ms，**4–8 倍** |
| [S16](#s16) 組合語言 | — | — | `.time_critical` | — | — | 模擬：指令數約一半 |

### 其他結構性招式（題目允許就用）

- **零複製**：處理直接在 DMA 寫入的陣列上做，不要再 `memcpy` 一份。
- **搬 + 校驗一次完成**：DMA 複製時開 sniffer（[S05](#s05)），複製完 checksum 也好了。
- **多個 PIO 狀態機平行**：同一支程式載一次（`pio_add_program`），`pio_sm_init` 到 2–4 個 SM，資料切成幾份、每份一對 DMA 通道（共 16 個通道夠用）。
- **超頻**：`set_sys_clock_khz(khz, true)`（`hardware/clocks.h`，已查證存在）。只有題目沒禁止、而且是量整體時間時才考慮；超過額定 150 MHz 的穩定性、需不需要 `vreg_set_voltage`、UART 鮑率會不會跟著變都**未查證**，一定要先在板子上測輸出正確。

---

<a id="s01"></a>
## S01 DMA 取代 memcpy / 複製迴圈

**題目特徵**：`for (i...) dst[i] = src[i];`、`memcpy`，或題目給一個像 transfer engine 的「描述 → 啟動 → 等待 → 取用」介面。

**Baseline**：`for (int i = 0; i < N; i++) dst[i] = src[i];`

**最終版**（已語法檢查，RISC-V 與 Arm）：

```c
// S01 用 DMA 取代 memcpy / for 複製（任意位元組長度，自動挑最大傳輸寬度）
#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/dma.h"

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

// ---- 測試 ----
#define N 4096
static uint32_t src[N], dst[N];
int main(void) {
    stdio_init_all();
    for (int i = 0; i < N; i++) src[i] = i * 2654435761u;
    uint64_t t0 = time_us_64();
    for (int i = 0; i < N; i++) dst[i] = src[i];        // baseline
    uint64_t t1 = time_us_64();
    int ch = dma_copy_async(dst, src, sizeof src);
    /* 這裡可以做別的事：DMA 搬的同時 CPU 是空的 */
    dma_copy_wait(ch);
    uint64_t t2 = time_us_64();
    printf("loop %llu us, dma %llu us, %s\n", (unsigned long long)(t1 - t0),
           (unsigned long long)(t2 - t1), memcmp(src, dst, sizeof src) ? "MISMATCH" : "ok");
}
```

**為什麼快**：DMA 是獨立的搬運單元，搬的時候 CPU 空著，可以同時算別的（題目真正要的通常是「重疊」：搬下一塊的同時處理上一塊 → 見 [optimization.md](optimization.md) 第 5.1 節乒乓）。

**加速**：估計單純搬移 1.5–3 倍；若搬移與計算重疊，總時間接近 max(搬, 算) 而不是兩者相加。**實機未量**。

**坑**：
- `count` 是**傳輸筆數**，不是位元組數（練習題特別示範這個錯）：`bytes >> size`。
- 位址要對齊傳輸大小；DMA_SIZE_8/16/32 的值是 0/1/2。
- 搬完前不要讀 dst、不要改 src。
- 太小的拷貝（< 64 bytes）設定成本比搬還貴，直接 `memcpy`。
- 通道只 claim 一次重複用，不要每次 claim。

---

<a id="s02"></a>
## S02 DMA 填值 / memset

**題目特徵**：把陣列清零或全部設成同一個值。

**Baseline**：`for (int i = 0; i < N; i++) buf[i] = 0xA5A5A5A5;`

**最終版**（已語法檢查）：

```c
// S02 用 DMA 填值（memset / 把陣列全設成同一個 word）
#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/dma.h"

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

#define N 4096
static uint32_t buf[N];
int main(void) {
    stdio_init_all();
    int ch = dma_fill32_async(buf, 0xA5A5A5A5u, N);
    dma_channel_wait_for_finish_blocking(ch);
    int ok = 1; for (int i = 0; i < N; i++) ok &= buf[i] == 0xA5A5A5A5u;
    dma_memset((uint8_t *)buf + 3, 0x5A, 1000);
    printf("%s\n", ok ? "ok" : "MISMATCH");
}
```

**為什麼快**：跟複製同一個引擎，只差「來源不遞增」（`read_increment = false`）。CPU 可以去做別的事。

**加速**：估計與 S01 相同等級，實機未量。

**坑**：來源變數（填的值）必須在 DMA 跑完前一直存在 → 放 `static`，不能是函式內的區域變數。位元組 memset 要把 byte 複製成 `0x01010101 * b`，頭尾不對齊的部分用 CPU。

---

<a id="s03"></a>
## S03 DMA → PIO → DMA 資料管線

> **要輾壓**：這題的全層疊加版（乒乓 + 雙核同時算）在 [S17](#s17)。

**題目特徵**：CPU 用 `pio_sm_put_blocking` / `pio_sm_get_blocking` 一筆一筆餵 PIO 再讀回（練習題的 echo 程式就是這個形狀）。

**Baseline**：
```c
for (int i = 0; i < N; i++) { pio_sm_put_blocking(pio, sm, in[i]); out[i] = pio_sm_get_blocking(pio, sm); }
```

**PIO 程式**（pioasm 組譯過；`xform_echo` 用 autopull + autopush 只要 2 指令/word）：

```pio
.program xform_invert
; 每個 word 位元反相：3 指令/word
.wrap_target
    pull block
    mov isr, ~osr
    push block
.wrap

.program xform_bitrev
; 每個 word 位元反轉（bit 0 ↔ bit 31）
.wrap_target
    pull block
    mov isr, ::osr
    push block
.wrap

.program xform_echo
; 原樣送回，autopull + autopush 只要 2 指令/word：out 32 → in 32
.wrap_target
    out x, 32
    in x, 32
.wrap

% c-sdk {
// 共用初始化：autopull/autopush 門檻 32，兩個 FIFO 都不 join
static inline void xform_init(PIO pio, uint sm, uint off, pio_sm_config c) {
    sm_config_set_out_shift(&c, true, true, 32);   // 右移、autopull、32
    sm_config_set_in_shift(&c, false, true, 32);   // 左移、autopush、32（32 位元整個進出，方向不影響）
    sm_config_set_clkdiv(&c, 1.0f);
    pio_sm_init(pio, sm, off, &c);
    pio_sm_set_enabled(pio, sm, true);
}
%}
```

**最終版**（已語法檢查）：

```c
// S03 DMA → PIO → DMA 管線：CPU 只描述工作，資料 in[] 經 PIO 變換後到 out[]
#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/dma.h"
#include "hardware/pio.h"
#include "xform.pio.h"            // pioasm 產生；CMake: pico_generate_pio_header(app ${CMAKE_CURRENT_LIST_DIR}/xform.pio)

#define N 4096
static uint32_t rev32(uint32_t x) {                 // 驗證用（GCC 沒有 __builtin_bitreverse32）
    x = ((x >> 1) & 0x55555555u) | ((x & 0x55555555u) << 1);
    x = ((x >> 2) & 0x33333333u) | ((x & 0x33333333u) << 2);
    x = ((x >> 4) & 0x0F0F0F0Fu) | ((x & 0x0F0F0F0Fu) << 4);
    return __builtin_bswap32(x);
}
static uint32_t in[N], out[N];

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

int main(void) {
    stdio_init_all();
    for (int i = 0; i < N; i++) in[i] = i * 2654435761u;

    PIO pio = pio0;
    uint sm = pio_claim_unused_sm(pio, true);
    uint off = pio_add_program(pio, &xform_bitrev_program);         // 換成 xform_invert / xform_echo 即可
    xform_init(pio, sm, off, xform_bitrev_program_get_default_config(off));

    uint64_t t0 = time_us_64();
    int tx, rx = pipe_start(pio, sm, in, out, N, &tx);
    dma_channel_wait_for_finish_blocking(rx);                        // 等「收」完，不是等送完
    uint64_t t1 = time_us_64();

    int bad = 0;
    for (int i = 0; i < N; i++) bad += out[i] != rev32(in[i]);
    printf("pipe %llu us, %s\n", (unsigned long long)(t1 - t0), bad ? "MISMATCH" : "ok");

    dma_channel_unclaim(tx); dma_channel_unclaim(rx);
    pio_sm_set_enabled(pio, sm, false);
    pio_remove_program(pio, &xform_bitrev_program, off);
    pio_sm_unclaim(pio, sm);
}
```

**為什麼快**：兩個 DMA 通道各自被 PIO 的 DREQ 節流（TX 不滿才送、RX 有資料才收），CPU 完全不參與；baseline 每筆要等 FIFO 來回。

**加速**：估計數倍（baseline 每筆至少兩次阻塞呼叫）。實機未量。

**坑**：
- 先啟動 **RX** 通道再啟動 TX，最後等的是 **RX** 完成（等 TX 完成時資料還在 PIO 裡）。
- DREQ：`pio_get_dreq(pio, sm, true)` = TX，`false` = RX。
- TX 的寫入位址 `&pio->txf[sm]`、RX 的讀取位址 `&pio->rxf[sm]`，兩者都**不遞增**。
- 換 PIO 程式只要改 `xform_xxx_program` 三處名稱。

---

<a id="s04"></a>
## S04 PIO 變換程式

**題目特徵**：給一支 PIO 程式要你看懂或改寫，或要 PIO 對每個 word 做某種轉換。

| 要做的事 | PIO 寫法（pioasm 組譯過） | 每 word 指令數 |
|---|---|---|
| 原樣送回 | `out x, 32` / `in x, 32`（autopull + autopush） | 2 |
| 位元反相 | `pull block` / `mov isr, ~osr` / `push block` | 3 |
| 位元反轉（bit0↔bit31） | `pull block` / `mov isr, ::osr` / `push block` | 3 |
| 位元組順序反轉 | 不用 PIO：DMA `channel_config_set_bswap(&c, true)` | 0 |
| 數 1 的個數 | 見 [cheatsheet.md](cheatsheet.md) 第 3 節 `popcount.pio`（逐位元迴圈，慢） | 約 3×32 |
| 加一個常數 | PIO 沒有加法（只有 `jmp x--` 遞減），交給 CPU 或用 `mov x, ~x` 技巧 | — |

**優化 PIO 的順序**：
1. `pull`/`push` 改 autopull/autopush（`sm_config_set_out_shift(&c, right, true, 32)`、`sm_config_set_in_shift(&c, right, true, 32)`）。
2. `.wrap_target` / `.wrap` 取代 `jmp` 回開頭。
3. 只單向傳資料時 `sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX)` 或 `_RX`，FIFO 4 → 8 層。
4. `sm_config_set_clkdiv(&c, 1.0f)` 全速。
5. 用 `.side_set` 同時控制腳位，省掉 `set` 指令。
6. 一次搬多位元（`out pins, 8`）而不是逐位元。
7. CPU 端改用 DMA（S03）。

**坑**：一個 PIO block 最多 32 條指令、4 個狀態機，RP2350 有 3 個 PIO（pio0/1/2）；`mov` 的 `~` 是反相、`::` 是位元反轉。

---

<a id="s05"></a>
## S05 Checksum / CRC32

**題目特徵**：要算整塊資料的 CRC 或加總。

**Baseline**：逐位元 CRC（每 byte 8 次迴圈）。

**最終版**（已語法檢查；兩個軟體版在 Linux 上與 zlib `crc32()` 比對 200 組隨機長度全部相同，`"123456789"` → `cbf43926`）：

```c
// S05 校驗和 / CRC32：DMA sniffer「搬的同時免費算」，對照查表軟體版
#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/dma.h"

// ---- 軟體 baseline：逐位元（慢） ----
uint32_t crc32_bitwise(const uint8_t *p, size_t n) {
    uint32_t c = 0xFFFFFFFFu;
    while (n--) { c ^= *p++; for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & -(c & 1)); }
    return ~c;                                         // 標準 CRC-32（zlib 同值）
}
// ---- 軟體優化：查表，每 byte 1 次查表（約快 8 倍） ----
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
    dma_sniffer_set_output_invert_enabled(true);      // 最後取反 = 標準 CRC-32 的 ~c（未查證：上機先比對軟體版）
    dma_sniffer_enable(ch, DMA_SNIFF_CTRL_CALC_VALUE_CRC32R, true);
    dma_channel_configure(ch, &c, &sink, p, n, true);
    dma_channel_wait_for_finish_blocking(ch);
    uint32_t r = dma_sniffer_get_data_accumulator();
    dma_sniffer_disable();
    dma_channel_unclaim(ch);
    return r;
}
// 簡單加總校驗：DMA_SNIFF_CTRL_CALC_VALUE_SUM，32 位元傳輸時一次加一個 word

int main(void) {
    stdio_init_all();
    static uint8_t buf[4096];
    for (int i = 0; i < 4096; i++) buf[i] = (uint8_t)(i * 7 + 1);
    crc32_init();
    printf("bit %08lx table %08lx dma %08lx\n", (unsigned long)crc32_bitwise(buf, 4096),
           (unsigned long)crc32_table(buf, 4096), (unsigned long)crc32_dma(buf, 4096));
    // "123456789" 的標準 CRC-32 = cbf43926
    printf("check %08lx\n", (unsigned long)crc32_table((const uint8_t *)"123456789", 9));
}
```

**為什麼快**：查表版每 byte 1 次查表取代 8 次迴圈；DMA sniffer 讓「搬資料」與「算 CRC」同時發生，CPU 0 成本。

**加速**：x86 實測 4 MB：逐位元 36.6 ms → 查表 8.8 ms（約 4 倍；逐位元版已用無分支寫法，若是 `if` 版差距更大）。sniffer 在已經要搬資料時等於免費。

**坑**：`dma_sniffer_set_output_invert_enabled(true)` 讓結果等於標準 CRC-32 是**推論（未查證）**——上機先印出三個值，`dma` 跟 `table` 不同就把 invert 拿掉、自己 `~` 一次。只要加總就用 `DMA_SNIFF_CTRL_CALC_VALUE_SUM`。

---

<a id="s06"></a>
## S06 大陣列運算切給兩顆核心

> **要輾壓**：先看能不能換演算法（[S18](#s18)）；資料若由 DMA/PIO 送進來，用 [S17](#s17) 讓搬運和兩核計算重疊。

**題目特徵**：一個大迴圈、各元素互相獨立（加總、找最大、轉換、計數）。

**Baseline**：單核心一個 `for`。

**最終版**（已語法檢查）：

```c
// S06 陣列運算切給兩顆核心（sum / 任何可分段的 reduce）
#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"

#define N 65536
static int32_t a[N];

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

int main(void) {
    stdio_init_all();
    multicore_launch_core1(core1_entry);                     // 只啟動一次
    for (int i = 0; i < N; i++) a[i] = (i * 37) % 1001 - 500;
    uint64_t t0 = time_us_64();
    int64_t ref = 0; for (int i = 0; i < N; i++) ref += a[i];        // baseline
    uint64_t t1 = time_us_64();
    int64_t s = sum_dual(a, N);
    uint64_t t2 = time_us_64();
    printf("single %llu us, dual %llu us, %s\n", (unsigned long long)(t1 - t0),
           (unsigned long long)(t2 - t1), s == ref ? "ok" : "MISMATCH");
}
```

**為什麼快**：兩顆核心同時跑一半；multicore FIFO 只傳 32 位元的工作指標，資料留在共用 SRAM。

**加速**：估計接近 2 倍（工作越大越接近），實機未量。

**坑**：
- `multicore_launch_core1` 只能呼叫一次，之後用 FIFO 派工。
- FIFO 每格 32 位元，傳指標或索引，不要傳資料本身。
- 兩核各寫各的區段，不要寫同一個變數（否則要鎖，見 S07）。
- 工作太小（幾百個元素）時，FIFO 來回的成本會吃掉收益。

---

<a id="s07"></a>
## S07 兩顆核心共用計數器（板子上的 critical section）

**題目特徵**：兩顆核心都要更新同一個變數，用 spin lock 或 critical section。

**Baseline**：(A) 把運算也包在鎖裡。

**最終版**（已語法檢查；四種寫法對照）：

```c
// S07 兩顆核心共用計數器 / 共用累加：鎖越短越好，最好不要共用
#include <stdio.h>
#include <stdatomic.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/sync.h"

#define ROUNDS 100000
static volatile int32_t counter;
static spin_lock_t *lk;
static atomic_int acounter;

static uint32_t work(uint32_t x) { for (int i = 0; i < 50; i++) x = x * 1664525u + 1013904223u; return x; }
static volatile uint32_t sink;

// (A) baseline：工作也包在鎖裡 → 兩核輪流等
static void run_bad(void) {
    for (int i = 0; i < ROUNDS; i++) {
        uint32_t save = spin_lock_blocking(lk);
        sink += work(i);
        counter++;
        spin_unlock(lk, save);
    }
}
// (B) 鎖只包「讀改寫」那一行
static void run_short(void) {
    for (int i = 0; i < ROUNDS; i++) {
        uint32_t w = work(i);
        uint32_t save = spin_lock_blocking(lk);
        counter++; sink += w;
        spin_unlock(lk, save);
    }
}
// (C) 不用鎖：硬體原子加（RISC-V amoadd / Arm ldrex-strex）
static void run_atomic(void) {
    for (int i = 0; i < ROUNDS; i++) { sink += work(i); atomic_fetch_add_explicit(&acounter, 1, memory_order_relaxed); }
}
// (D) 最快：各自累加，最後只合併一次
static void run_local(void) {
    int32_t local = 0;
    for (int i = 0; i < ROUNDS; i++) { sink += work(i); local++; }
    uint32_t save = spin_lock_blocking(lk);
    counter += local;
    spin_unlock(lk, save);
}

static void (*volatile core1_fn)(void);
static void core1_entry(void) {
    while (true) { multicore_fifo_pop_blocking(); core1_fn(); multicore_fifo_push_blocking(0); }
}
static uint64_t both(void (*f)(void)) {
    counter = 0; atomic_store(&acounter, 0);
    core1_fn = f;
    uint64_t t0 = time_us_64();
    multicore_fifo_push_blocking(0);   // 叫 core1 開始
    f();                               // core0 同時跑
    multicore_fifo_pop_blocking();     // 等 core1
    return time_us_64() - t0;
}
int main(void) {
    stdio_init_all();
    lk = spin_lock_init(spin_lock_claim_unused(true));
    multicore_launch_core1(core1_entry);
    uint64_t t;                         // 先量完再印（參數求值順序不固定）
    t = both(run_bad);    printf("bad    %6llu us  %ld\n", (unsigned long long)t, (long)counter);
    t = both(run_short);  printf("short  %6llu us  %ld\n", (unsigned long long)t, (long)counter);
    t = both(run_atomic); printf("atomic %6llu us  %d\n",  (unsigned long long)t, atomic_load(&acounter));
    t = both(run_local);  printf("local  %6llu us  %ld\n", (unsigned long long)t, (long)counter);
}
```

**為什麼快**：(B) 鎖只包讀改寫一行 → 兩核大部分時間平行；(C) 硬體原子指令（RISC-V `amoadd`、Arm `ldrex/strex`）不用鎖；(D) 各自累加最後合併一次，幾乎零競爭。同樣道理在 Linux 實測（S14）：長鎖 87 ms → 本地累加 34 ms。

**加速**：(A)→(D) 估計接近 2 倍（工作越重越明顯），實機未量。

**坑**：
- RP2350 因 erratum E2，SDK 預設用**軟體** spin lock（`PICO_USE_SW_SPIN_LOCKS`），內部就是用原子指令實作。
- `spin_lock_blocking` 會關中斷並回傳舊狀態，必須用 `spin_unlock(lk, save)` 還回去。
- 只有一個計數器時優先用 `<stdatomic.h>`；要保護多個欄位一起改才用鎖。

---

<a id="s08"></a>
## S08 生產者 / 消費者：無鎖 SPSC 環形佇列

**題目特徵**：一邊產生資料、一邊處理；或題目用 mutex + queue 在兩個執行緒/核心間傳資料。

**Baseline**：每筆都上鎖的佇列（或 `queue_add_blocking` 每次一筆）。

**佇列本體**（C11 atomics，RP2350 與 Linux 共用；Linux 上以 ThreadSanitizer 跑 200 萬筆無警告，5000 萬筆總和正確）：

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

**RP2350 雙核心使用範例**（已語法檢查）：

```c
// S08 core0 產生資料、core1 處理：無鎖 SPSC 佇列（取代 mutex + queue）
#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "spsc_ring.h"

#define TOTAL 100000u
static ring_t q;
static volatile uint64_t core1_sum;
static volatile bool core1_done;

static void core1_entry(void) {
    uint32_t tmp[32]; uint64_t s = 0; uint32_t got = 0;
    while (got < TOTAL) {
        unsigned n = ring_pop_many(&q, tmp, 32);
        for (unsigned i = 0; i < n; i++) s += tmp[i];
        got += n;
    }
    core1_sum = s; core1_done = true;
}
int main(void) {
    stdio_init_all();
    multicore_launch_core1(core1_entry);
    uint64_t t0 = time_us_64(), ref = 0;
    for (uint32_t i = 0; i < TOTAL; i++) {
        uint32_t v = i * 2654435761u; ref += v;
        while (!ring_push(&q, v)) tight_loop_contents();   // 滿了就等
    }
    while (!core1_done) tight_loop_contents();
    printf("%llu us %s\n", (unsigned long long)(time_us_64() - t0), core1_sum == ref ? "ok" : "MISMATCH");
}
```

**為什麼快**：只有一個寫 head、一個寫 tail，不需要鎖；acquire/release 保證資料先寫好才被看到；批次 `ring_pop_many` 少碰共享索引。

**加速**：Linux 實測 5000 萬筆 1.27 秒（兩執行緒）。對 mutex 佇列的倍數沒有量。

**坑**：只能**一個**生產者、**一個**消費者；容量必須是 2 的次方；多對多改用 SDK 的 `pico/util/queue.h`。

---

<a id="s09"></a>
## S09 位元運算核心（popcount、parity、反轉、欄位）

**題目特徵**：對 32 位元整數做位元操作，或練習題 `demo_asm.c` 那類 AND/OR/XOR/移位。

**Baseline**：逐位元 `for (b = 0; b < 32; b++)`。

**最終版**（Linux 上 100 萬組隨機輸入對照逐位元參考實作，0 筆不同；RISC-V 與 Arm 語法檢查過）：

```c
// S09 位元運算核心（RP2350 RISC-V 的 Zbb/Zbkb 會變成單一指令；Arm 也有對應寫法）
#include <stdint.h>
#include <stddef.h>
// 整個陣列有幾個 1
static inline uint32_t popcnt_array_naive(const uint32_t *a, size_t n) {
    uint32_t c = 0; for (size_t i = 0; i < n; i++) for (int b = 0; b < 32; b++) c += (a[i] >> b) & 1; return c;
}
static inline uint32_t popcnt_array(const uint32_t *a, size_t n) {
    uint32_t c0 = 0, c1 = 0; size_t i = 0;
    for (; i + 2 <= n; i += 2) { c0 += __builtin_popcount(a[i]); c1 += __builtin_popcount(a[i + 1]); }
    if (i < n) c0 += __builtin_popcount(a[i]);
    return c0 + c1;
}
// 漢明距離（兩陣列不同的位元數）
static inline uint32_t hamming(const uint32_t *a, const uint32_t *b, size_t n) {
    uint32_t c = 0; for (size_t i = 0; i < n; i++) c += __builtin_popcount(a[i] ^ b[i]); return c;
}
// 奇偶校驗：1 的個數是奇數回傳 1
static inline uint32_t parity_naive(uint32_t x) { uint32_t p = 0; while (x) { p ^= x & 1; x >>= 1; } return p; }
static inline uint32_t parity(uint32_t x) { return __builtin_popcount(x) & 1; }        // GCC 也有 __builtin_parity
// floor(log2(x))，x>0
static inline int ilog2_naive(uint32_t x) { int r = -1; while (x) { x >>= 1; r++; } return r; }
static inline int ilog2(uint32_t x) { return 31 - __builtin_clz(x); }                  // x==0 未定義
// 下一個 2 的次方（x>=1）
static inline uint32_t next_pow2(uint32_t x) { return x <= 1 ? 1 : 1u << (32 - __builtin_clz(x - 1)); }
// 取出 bits [hi:lo]
static inline uint32_t bits_get(uint32_t x, int hi, int lo) { return (x >> lo) & ((2u << (hi - lo)) - 1); }
// 設定 bits [hi:lo] = v
static inline uint32_t bits_set(uint32_t x, int hi, int lo, uint32_t v) {
    uint32_t m = ((2u << (hi - lo)) - 1) << lo; return (x & ~m) | ((v << lo) & m);
}
// 位元組順序反轉 / 位元反轉
static inline uint32_t bswap(uint32_t x) { return __builtin_bswap32(x); }               // RV: rev8, Arm: rev
static inline uint32_t bitrev(uint32_t x) {                                             // RV: rev8+brev8, Arm: rbit（clang 實測）
    x = ((x >> 1) & 0x55555555u) | ((x & 0x55555555u) << 1);
    x = ((x >> 2) & 0x33333333u) | ((x & 0x33333333u) << 2);
    x = ((x >> 4) & 0x0F0F0F0Fu) | ((x & 0x0F0F0F0Fu) << 4);
    return __builtin_bswap32(x);
}
// 循環左移（RV Zbb: rol/rori；Arm: ror）
static inline uint32_t rotl(uint32_t x, unsigned k) { k &= 31; return (x << k) | (x >> ((32 - k) & 31)); }
// 最低位的 1 / 清掉最低位的 1
static inline uint32_t lowest_set(uint32_t x) { return x & -x; }
static inline uint32_t clear_lowest(uint32_t x) { return x & (x - 1); }
// 是不是 2 的次方
static inline int is_pow2(uint32_t x) { return x && !(x & (x - 1)); }
```

**clang -O2 對 RP2350 RISC-V 實際產生的指令**：`parity` → `cpop` + `andi`；`ilog2` → `clz` + `xori`；`rotl` → `rol`；`bitrev` → `rev8` + `brev8`；`clear_lowest` → 2 條；`lowest_set` → 2 條。

**加速**：popcount 陣列逐位元 vs builtin，x86 實測 17.7 ms → 6.6 ms（x86 沒開 `-mpopcnt`）；在 RP2350 RISC-V 上 `cpop` 是 1 條 1 週期指令，逐位元要約 32 圈 → 估計 20 倍以上。

**坑**：`__builtin_clz(0)`、`__builtin_ctz(0)` 未定義，先判斷 0；位移量 ≥ 32 是未定義行為；用 `uint32_t` 不要用 `int`（右移有號數會補符號位）。GCC 也支援 Zbb 產生 `cpop`，但需要 `-march` 帶 `_zbb`（SDK 的 rp2350-riscv 預設有）。

---

<a id="s10"></a>
## S10 查表 vs 現算

**題目特徵**：同一個函式被呼叫幾百萬次，而且輸入範圍很小（8 位元）或函式很貴（sin、CRC）。

**最終版**（Linux 上比對過；`sin_q15` 對雙精度 sin 的最大誤差 4.1/32767 = 0.012%）：

```c
// S10 查表 vs 現算：值域小、函式貴 → 查表；值域大或函式本來就 1 指令 → 現算
#include <stdint.h>
#include <stddef.h>
// (1) 8 位元 → 8 位元任意轉換（例：位元反轉、gamma、編碼）：先建 256 表，之後每 byte 一次讀取
static uint8_t REV8[256];
static inline uint8_t rev8_compute(uint8_t b) { b = (b >> 4) | (b << 4); b = ((b & 0xCC) >> 2) | ((b & 0x33) << 2); return ((b & 0xAA) >> 1) | ((b & 0x55) << 1); }
static inline void lut_init(void) { for (int i = 0; i < 256; i++) REV8[i] = rev8_compute((uint8_t)i); }
static inline void map_bytes(uint8_t *dst, const uint8_t *src, size_t n) { for (size_t i = 0; i < n; i++) dst[i] = REV8[src[i]]; }
// (2) 每次都 % 或 / 同一個小範圍：先算好表
//     例：x 在 0..1023，要 x*x % 1000 → 建 1024 項表
// (3) 三角函數：Q15 正弦表 256 項 + 線性內插，不用浮點（RISC-V 沒 FPU 時很有用）
static int16_t SIN_Q15[257];                                  // 多一項方便內插
// 建表用浮點只做一次（開機時），之後全整數
static inline void sin_init(double (*sinf_fn)(double)) {
    for (int i = 0; i <= 256; i++) SIN_Q15[i] = (int16_t)(sinf_fn(i * 6.283185307179586 / 256) * 32767.0);
}
// phase：0..65535 代表 0..2π；回傳 Q15
static inline int32_t sin_q15(uint16_t phase) {
    uint32_t idx = phase >> 8, frac = phase & 0xFF;
    int32_t a = SIN_Q15[idx], b = SIN_Q15[idx + 1];
    return a + (((b - a) * (int32_t)frac) >> 8);
}
```

**什麼時候查表會輸**（實測）：位元組位元反轉在 x86 上現算 3.1 ms、查表 4.5 ms（現算被向量化）；在 RP2350 RISC-V 上 clang 把 `rev8_compute` 編成 `rev8` + `brev8` + `srli` 共 3 條，查表不會比較快。**CPU 有單一指令能做的，不要查表。**

**什麼時候查表會贏**：sin/cos/log（尤其 RISC-V 沒有 FPU）、CRC（S05）、任意不規則的映射、Arm 上的 popcount（Cortex-M33 沒有 popcount 指令）。

**坑**：表要放 SRAM。非 `const` 的全域陣列預設就在 SRAM；`const` 表會進唯讀區段，預設的 flash 程式會從 flash 經 XIP 快取讀（較慢）→ 熱的小表拿掉 `const`，或整支用 `pico_set_binary_type(app copy_to_ram)`。表太大會擠爆 520 KB SRAM。

---

<a id="s11"></a>
## S11 排序與搜尋

**題目特徵**：排序、找第 k 小、中位數、在已排序資料中查找。

**Baseline**：`qsort` 或泡沫排序。

**最終版**（Linux 上與 `qsort`、標準二分搜尋對照，0 筆不同）：

```c
// S11 排序與搜尋：看資料範圍選演算法，不要一律 qsort
#include <stdint.h>
#include <stddef.h>
#include <string.h>
// (1) n 很小（<= 16~32）：插入排序，沒有函式指標呼叫
static inline void isort_i32(int32_t *a, int n) {
    for (int i = 1; i < n; i++) { int32_t v = a[i]; int j = i - 1; while (j >= 0 && a[j] > v) { a[j + 1] = a[j]; j--; } a[j + 1] = v; }
}
// (2) 值域小（例：uint8_t、0..999）：計數排序 O(n + K)
static inline void csort_u8(uint8_t *a, size_t n) {
    uint32_t cnt[256] = {0};
    for (size_t i = 0; i < n; i++) cnt[a[i]]++;
    size_t k = 0;
    for (int v = 0; v < 256; v++) { memset(a + k, v, cnt[v]); k += cnt[v]; }
}
// (3) 32 位元整數、n 大：基數排序（4 輪 × 8 位元），O(n)，需要同大小暫存
static inline void rsort_u32(uint32_t *a, uint32_t *tmp, size_t n) {
    for (int sh = 0; sh < 32; sh += 8) {
        uint32_t cnt[256] = {0};
        for (size_t i = 0; i < n; i++) cnt[(a[i] >> sh) & 255]++;
        uint32_t sum = 0; for (int v = 0; v < 256; v++) { uint32_t c = cnt[v]; cnt[v] = sum; sum += c; }
        for (size_t i = 0; i < n; i++) tmp[cnt[(a[i] >> sh) & 255]++] = a[i];
        uint32_t *t = a; a = tmp; tmp = t;            // 4 輪後結果回到原陣列
    }
}
// 有號 int32 用基數排序：先 x ^ 0x80000000 轉成無號順序，排完再轉回
// (4) 已排序陣列找第一個 >= key 的位置（lower_bound），無分支版
static inline size_t lower_bound_i32(const int32_t *a, size_t n, int32_t key) {
    const int32_t *base = a;
    while (n > 1) { size_t half = n / 2; base = (base[half - 1] < key) ? base + half : base; n -= half; }
    return (size_t)(base - a) + (n == 1 && *base < key);
}
// (5) 只要前 k 小 / 中位數：不用全排，部分選擇（quickselect）
static inline int32_t kth_smallest(int32_t *a, int n, int k) {   // 會改動陣列；k 從 0 起
    int lo = 0, hi = n - 1;
    while (lo < hi) {
        int32_t p = a[(lo + hi) / 2]; int i = lo, j = hi;
        while (i <= j) { while (a[i] < p) i++; while (a[j] > p) j--; if (i <= j) { int32_t t = a[i]; a[i] = a[j]; a[j] = t; i++; j--; } }
        if (k <= j) hi = j; else if (k >= i) lo = i; else return a[k];
    }
    return a[k];
}
```

**實測（x86，100 萬筆）**：`uint32_t` qsort 113 ms → 基數排序 16 ms（約 7 倍）；`uint8_t` qsort 121 ms → 計數排序 0.8 ms（約 150 倍）。

**怎麼選**：

| 資料 | 用 |
|---|---|
| n ≤ 32 | `isort_i32` 插入排序 |
| 值域小（byte、0..999） | 計數排序 |
| 32 位元整數、n 大、有額外 RAM | 基數排序 |
| 只要第 k 小或中位數 | `kth_smallest`（平均 O(n)，會改動陣列） |
| 已排序、要查很多次 | `lower_bound_i32` |
| 要查很多次但沒排序 | 先排序一次再二分；或雜湊（見 [algorithms.md](algorithms.md)） |

**坑**：基數排序需要同大小暫存陣列（RP2350 記憶體有限）；有號整數要先 `x ^ 0x80000000`；`qsort` 的比較函式寫 `return a - b` 會溢位，要寫 `(a > b) - (a < b)`。

---

<a id="s12"></a>
## S12 ROS 2 processor：低延遲、高吞吐

**題目特徵**：練習題 section2-1 的 `processor.py`（`/number` → ×2 → `/double`），或要你讓資料流更快、不掉資料。

**Baseline**：練習題原版，每則訊息 `print(..., flush=True)`、每則 new 一個 `Int32()`。

**最終版 Python**（`py_compile` 通過，名稱對照 rclpy jazzy）：

```python
"""S12 低延遲 processor：/number → ×2 → /double（對照 demo 的 processor.py）"""
import rclpy
from rclpy.node import Node
from rclpy.qos import QoSProfile, ReliabilityPolicy, HistoryPolicy
from std_msgs.msg import Int32

IN_TOPIC, OUT_TOPIC = '/number', '/double'
# 發布端保持 RELIABLE：若對方訂閱端是 RELIABLE 而我們發 BEST_EFFORT，會完全連不上
OUT_QOS = QoSProfile(depth=10, reliability=ReliabilityPolicy.RELIABLE, history=HistoryPolicy.KEEP_LAST)
# 訂閱端：題目要「不能漏」→ RELIABLE；要「越新越好」→ BEST_EFFORT + depth=1（兩種發布端都相容）
IN_QOS = QoSProfile(depth=10, reliability=ReliabilityPolicy.RELIABLE, history=HistoryPolicy.KEEP_LAST)


class Processor(Node):
    def __init__(self):
        super().__init__('processor')
        self.pub = self.create_publisher(Int32, OUT_TOPIC, OUT_QOS)
        self.out = Int32()                                   # 重複使用，不要每則 new
        self.create_subscription(Int32, IN_TOPIC, self.on_msg, IN_QOS)

    def on_msg(self, m):
        # 拿掉 print(..., flush=True)：每則訊息一次系統呼叫，是最大的延遲來源
        self.out.data = m.data * 2
        self.pub.publish(self.out)


def main():
    rclpy.init()
    node = Processor()
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        node.destroy_node()
        rclpy.try_shutdown()


if __name__ == '__main__':
    main()
```

**最終版 C++**（吞吐量要求很高時；名稱對照 rclcpp jazzy，**沒有實際編譯**）：

```cpp
// S12 C++ 版 processor（吞吐量要求高時用；名稱對照 rclcpp jazzy 原始碼，未實際編譯）
#include <memory>
#include "rclcpp/rclcpp.hpp"
#include "std_msgs/msg/int32.hpp"

class Processor : public rclcpp::Node {
public:
  Processor() : Node("processor") {
    pub_ = create_publisher<std_msgs::msg::Int32>("/double", 10);
    sub_ = create_subscription<std_msgs::msg::Int32>("/number", 10,
      [this](const std_msgs::msg::Int32 & m) {
        std_msgs::msg::Int32 out;
        out.data = m.data * 2;
        pub_->publish(out);
      });
  }
private:
  rclcpp::Publisher<std_msgs::msg::Int32>::SharedPtr pub_;
  rclcpp::Subscription<std_msgs::msg::Int32>::SharedPtr sub_;
};

int main(int argc, char ** argv) {
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<Processor>());
  rclcpp::shutdown();
  return 0;
}
```

**為什麼快**：`print(flush=True)` 每則一次系統呼叫，是 Python 版最大的延遲來源；重複使用訊息物件省配置；C++ 省掉 Python 直譯與 GIL。

**加速**：未量（考場外沒有 ROS 環境）。

**量測與除錯**：`ros2 topic hz /double`、`ros2 topic bw /double`、`ros2 topic delay /double`（訊息要有 header 才行，`Int32` 沒有）、`ros2 topic info -v /double`（看兩端 QoS）、`ros2 node info /processor`。

**坑**：
- **QoS 不相容就完全收不到**：訂閱端 RELIABLE + 發布端 BEST_EFFORT → 連不上。不確定時兩邊都用預設（depth 10、RELIABLE）。
- topic 名稱有沒有開頭 `/`、大小寫，要跟題目完全一樣。
- 新開的終端機要先 `source /opt/ros/jazzy/setup.bash`（練習環境的 `.bashrc` 已處理）。

---

<a id="s13"></a>
## S13 pthread 平行 reduce

**題目特徵**：Linux 上大陣列加總 / 最大值 / 計數，要你用多執行緒加速。

**Baseline**：單執行緒迴圈；或多執行緒但每個元素都鎖一次。

**最終版**（Linux 實測）：

```c
// S13 pthread 平行 reduce（sum / max / count）：各算各的，最後合併一次
// 編譯：gcc -O3 -pthread s13_reduce.c -o reduce
#define _GNU_SOURCE
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#define N (1 << 26)
#define MAXT 64
static int32_t *a;
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
// baseline：所有執行緒搶同一把鎖
static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER; static int64_t shared;
static void *worker_bad(void *arg) {
    part_t *p = arg;
    for (int i = p->lo; i < p->hi; i++) { pthread_mutex_lock(&mu); shared += a[i]; pthread_mutex_unlock(&mu); }
    return NULL;
}
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }

int main(void) {
    a = malloc(sizeof(int32_t) * N);
    for (int i = 0; i < N; i++) a[i] = (int32_t)(((uint32_t)i * 37u) % 1001u) - 500;
    int nt = (int)sysconf(_SC_NPROCESSORS_ONLN); if (nt > MAXT) nt = MAXT;   // 執行緒數 = 核心數

    double t0 = now(); int64_t ref = 0; for (int i = 0; i < N; i++) ref += a[i]; double t1 = now();
    int64_t s = sum_parallel(nt); double t2 = now();
    // 鎖版只跑 1/64 的資料，不然太久
    pthread_t th[MAXT]; int M = N / 64;
    for (int t = 0; t < nt; t++) { part[t].lo = M * t / nt; part[t].hi = M * (t + 1) / nt; pthread_create(&th[t], NULL, worker_bad, &part[t]); }
    for (int t = 0; t < nt; t++) pthread_join(th[t], NULL);
    double t3 = now();
    printf("threads=%d single %.1fms parallel %.1fms (%s) | mutex-per-item on 1/64 data %.1fms\n",
           nt, (t1 - t0) * 1e3, (t2 - t1) * 1e3, s == ref ? "ok" : "MISMATCH", (t3 - t2) * 1e3);
    free(a);
}
```

**實測（4 vCPU，6700 萬個 int32）**：單執行緒 36–52 ms → 4 執行緒 10–14 ms（約 3–4 倍）；每個元素都上鎖的版本只跑 1/64 的資料就要 66–76 ms（換算整份要 4 秒以上）。

**為什麼快**：每個執行緒用區域變數累加、最後寫一次；`_Alignas(64)` 讓每個執行緒的結果各占一條快取行，避免偽共享。

**坑**：執行緒數 = 可用核心數（`sysconf(_SC_NPROCESSORS_ONLN)`），多開沒用；`i * 37` 這種運算會整數溢位（GCC 實際警告過），用 `uint32_t`；`-pthread` 要加在編譯與連結。

---

<a id="s14"></a>
## S14 修好練習題的 critical section

**題目特徵**：練習題 section2-3 `critical-section/main.c`：兩個執行緒對同一個 counter 加 1，沒鎖會少、鎖太長會慢。

**最終版**（Linux 實測）：

```c
// S14 修好 demo 的 critical section：正確 + 快（對照 section2-3/critical-section/main.c）
// 編譯：gcc -O2 -pthread s14_critical.c -o crit
#define _GNU_SOURCE
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

#define WORKERS 2
#define ROUNDS  20000
static volatile long counter;
static atomic_long acounter;
static pthread_spinlock_t lock;
static uint32_t work(uint32_t x) { for (int i = 0; i < 2000; i++) x = x * 1664525u + 1013904223u; return x; }
static _Atomic uint32_t sink;                     // 原 demo 的 sink 自己也有資料競爭，這裡改 atomic

// 原 demo 第 3 種（慢）：工作放在鎖裡
static void *w_long(void *a) { (void)a; for (int i = 0; i < ROUNDS; i++) { pthread_spin_lock(&lock); sink += work(i); counter++; pthread_spin_unlock(&lock); } return 0; }
// 原 demo 第 2 種（對）：只鎖 counter++
static void *w_short(void *a) { (void)a; for (int i = 0; i < ROUNDS; i++) { uint32_t w = work(i); pthread_spin_lock(&lock); counter++; pthread_spin_unlock(&lock); sink += w; } return 0; }
// 更好：不用鎖，原子加
static void *w_atomic(void *a) { (void)a; for (int i = 0; i < ROUNDS; i++) { sink += work(i); atomic_fetch_add_explicit(&acounter, 1, memory_order_relaxed); } return 0; }
// 最好：各自累加，最後鎖一次（或 atomic 一次）
static void *w_local(void *a) { (void)a; long n = 0; uint32_t s = 0;
    for (int i = 0; i < ROUNDS; i++) { s += work(i); n++; }
    pthread_spin_lock(&lock); counter += n; pthread_spin_unlock(&lock); sink += s; return 0; }

static double run(void *(*f)(void *)) {
    struct timespec t0, t1; pthread_t th[WORKERS];
    counter = 0; atomic_store(&acounter, 0);
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (int i = 0; i < WORKERS; i++) pthread_create(&th[i], 0, f, 0);
    for (int i = 0; i < WORKERS; i++) pthread_join(th[i], 0);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    return (t1.tv_sec - t0.tv_sec) * 1e3 + (t1.tv_nsec - t0.tv_nsec) / 1e6;
}
int main(void) {
    pthread_spin_init(&lock, PTHREAD_PROCESS_PRIVATE);
    double ms;
    ms = run(w_long);   printf("lock, long hold   %6ld  %6.1f ms\n", counter, ms);
    ms = run(w_short);  printf("lock, short hold  %6ld  %6.1f ms\n", counter, ms);
    ms = run(w_atomic); printf("atomic, no lock   %6ld  %6.1f ms\n", (long)atomic_load(&acounter), ms);
    ms = run(w_local);  printf("local + merge     %6ld  %6.1f ms\n", counter, ms);
    pthread_spin_destroy(&lock);
}
```

**實測（4 vCPU，兩次）**：

```
lock, long hold    40000    87.4 ms / 83.9 ms
lock, short hold   40000    42.7 ms / 44.7 ms
atomic, no lock    40000    46.1 ms / 49.1 ms
local + merge      40000    34.5 ms / 34.3 ms
```

**結論**：鎖只包共享資料那一行（2 倍）；能不共用就不共用（再快 25%）；atomic 在這題跟短鎖差不多，不是萬靈丹。

**坑**：原 demo 的 `sink += work(i)` 在無鎖版本本身也有資料競爭，正式寫法要改成 atomic 或各自累加。

---

<a id="s15"></a>
## S15 矩陣乘法 / 2D 陣列：迴圈順序

**題目特徵**：三層迴圈、`A[i][k] * B[k][j]`、影像處理、2D 陣列走訪。

**最終版**（Linux 上 512×512 與 7×7 都與 naive 比對相同）：

```c
// S15 矩陣乘法 / 2D 陣列走訪：迴圈順序決定快取命中
#include <stdint.h>
#include <string.h>
// baseline：i-j-k，B 每次跳一整列（cache miss 很多）
static inline void mm_naive(int n, const int32_t *A, const int32_t *B, int32_t *C) {
    for (int i = 0; i < n; i++)
        for (int j = 0; j < n; j++) { int32_t s = 0; for (int k = 0; k < n; k++) s += A[i*n+k] * B[k*n+j]; C[i*n+j] = s; }
}
// 優化 1：換成 i-k-j，最內層 B、C 都連續存取（編譯器還能向量化）
static inline void mm_ikj(int n, const int32_t *restrict A, const int32_t *restrict B, int32_t *restrict C) {
    memset(C, 0, sizeof(int32_t) * n * n);
    for (int i = 0; i < n; i++)
        for (int k = 0; k < n; k++) { int32_t a = A[i*n+k]; const int32_t *b = &B[k*n]; int32_t *c = &C[i*n];
            for (int j = 0; j < n; j++) c[j] += a * b[j]; }
}
// 分塊（tile）：這台 x86 實測反而比 ikj 慢（內層多了邊界判斷、無法向量化），沒量過別用
// 轉置：讀連續、寫跳躍（或分塊轉置），比 C[j][i]=A[i][j] 逐一跳好
```

**實測（512×512 int32，GCC -O3）**：naive i-j-k 115 ms → i-k-j 23 ms（約 5 倍）。

**為什麼快**：最內層改成連續存取，快取命中、編譯器能向量化。

**RP2350 上**：SRAM 沒有資料快取，迴圈順序的差別會小很多；主要收益來自 `restrict`、把 `A[i*n+k]` 提到內層迴圈外、指標遞增取代乘法索引。實機未量。

**坑**：i-k-j 要先把 C 清零；有 `restrict` 才保證不重疊（A、B、C 是同一塊時不能用）。

---

<a id="s16"></a>
## S16 手寫 RISC-V 組合語言

**題目特徵**：要你寫或優化一段 RV32 組合語言（練習題附了 RISC-V 指令集手冊連結）。

**先試**：用 C 寫、`-O3` 編譯、看 `.dis`。clang 對 RP2350 `-march` 已經會產生 `cpop`、`sh2add`、`min`、`rev8` 等（見 [optimization.md](optimization.md) 第 3.4 節）。手寫只有在編譯器明顯做不好時才值得。

**最終版**（clang 組譯、ld.lld 連結後，用自寫的 RV32 指令模擬器跑 n = 0~39、1000~1003 的隨機陣列，結果與 Python 參考值全部相同）：

```asm
# S16 手寫 RISC-V 組合語言（RP2350 Hazard3，rv32imac + Zba/Zbb/Zbs/Zbkb）
# uint32_t asm_sum4(const uint32_t *a, uint32_t n);  4 路展開，n 任意
#   a0 = a, a1 = n, 回傳 a0
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

# uint32_t asm_popcount_array(const uint32_t *a, uint32_t n);  用 Zbb 的 cpop
    .section .time_critical.asm_popcount_array, "ax"
    .globl asm_popcount_array
    .type  asm_popcount_array, @function
asm_popcount_array:
    sh2add  a2, a1, a0
    li      t0, 0
    beq     a0, a2, 2f
1:  lw      t1, 0(a0)
    cpop    t1, t1              # 1 個 cycle（Hazard3 文件）
    add     t0, t0, t1
    addi    a0, a0, 4
    bne     a0, a2, 1b
2:  mv      a0, t0
    ret
    .size asm_popcount_array, .-asm_popcount_array
```

**指令數（模擬器數的）**：n = 1000 時 `asm_sum4` 執行 2509 條；最簡單的逐筆迴圈（`lw`、`add`、`addi`、`addi`、`bnez`）約 5000 條 → 約一半。`asm_popcount_array` 每個 word 5 條。

**為什麼快**：4 路展開讓分支與指標更新攤到 4 筆；兩個累加器打斷相依（Hazard3 文件：load 結果下一條就用會多 1 週期）；用位址比較省掉計數器；`sh2add` 一條算位址。

**C 端宣告**：`uint32_t asm_sum4(const uint32_t *a, uint32_t n);`，CMake 把 `.S` 加進 `add_executable` 的來源即可。

**坑**：呼叫慣例 a0–a7 傳參數、a0 回傳、t0–t6 可以隨便用、s0–s11 用了要先存後還；`.time_critical.*` 區段名稱才會被 SDK 放進 RAM；Arm 版（`rp2350-arm-s`）不能用這段，要用 C 版。

---

<a id="s17"></a>
## S17 全層疊加：DMA + PIO + 兩顆核心同時跑（串流處理）

**題目特徵**：資料要先經過 PIO（或任何周邊）變換，再由 CPU 算出結果；原版是 `for` 迴圈裡 `pio_sm_put_blocking` → `pio_sm_get_blocking` → 計算。這是練習題 echo + transfer engine 的組合，也是「整體系統效能」最可能的考法。

**Baseline**：
```c
for (int i = 0; i < N; i++) {
    pio_sm_put_blocking(pio, sm, in[i]);
    sum += work(pio_sm_get_blocking(pio, sm));
}
```
搬運、PIO、計算三件事排隊，CPU 大部分時間在等 FIFO。

**疊加了哪幾層**：
1. TX DMA 一次把整個輸入送進 PIO（CPU 不碰）。
2. RX 用兩個 DMA 通道乒乓，每塊 1024 字；一塊收完由 IRQ 立刻啟動另一個通道收下一塊，PIO 幾乎不停。
3. core0 處理偶數塊、core1 處理奇數塊，**和 DMA、PIO 同時進行**。
4. 處理直接在 DMA 的目的陣列上做（零複製）；處理函式與 IRQ 放 RAM；兩個累加器。

**最終版**（已語法檢查，RISC-V 與 Arm；乒乓排程用 Python 模型隨機 IRQ 延遲驗證過：每塊恰好寫一次、順序正確、結束時通道都停下；**沒在實機上跑**）：

```c
// S17 全層疊加：TX DMA → PIO → RX DMA（乒乓雙緩衝）＋ 兩顆核心同時處理 ＋ 處理函式放 RAM
// 原版：CPU 一個字一個字 put/get，再自己算 → 搬運、PIO、計算三件事排隊
// 疊加版：DMA 搬、PIO 變換、core0 算偶數塊、core1 算奇數塊，四件事同時進行
#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/dma.h"
#include "hardware/pio.h"
#include "hardware/irq.h"
#include "hardware/sync.h"
#include "xform.pio.h"            // CMake: pico_generate_pio_header(app ${CMAKE_CURRENT_LIST_DIR}/xform.pio)
                                  // target_link_libraries(app pico_stdlib pico_multicore hardware_dma hardware_pio)
#define N    (16 * 1024)          // 總字數，必須是 BLK 的倍數
#define BLK  1024                 // 每塊字數
#define NBLK (N / BLK)

static uint32_t in[N], out[N];
static int rx_ch[2];
static volatile uint32_t blocks_ready;      // 已收完的塊數，只有 DMA IRQ 會寫
static uint64_t core1_sum;

// 每個字要做的運算：換成題目的運算
static inline uint32_t work(uint32_t x) {
    x *= 0x9E3779B1u;
    x ^= x >> 15;
    return (uint32_t)__builtin_popcount(x);
}

// 放 RAM、兩個累加器
static uint64_t __not_in_flash_func(process_block)(const uint32_t *p) {
    uint64_t s0 = 0, s1 = 0;
    for (int i = 0; i < BLK; i += 2) { s0 += work(p[i]); s1 += work(p[i + 1]); }
    return s0 + s1;
}

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

static void rx_setup(PIO pio, uint sm) {
    for (int j = 0; j < 2; j++) {
        rx_ch[j] = dma_claim_unused_channel(true);
        dma_channel_config c = dma_channel_get_default_config(rx_ch[j]);
        channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
        channel_config_set_read_increment(&c, false);
        channel_config_set_write_increment(&c, true);
        channel_config_set_dreq(&c, pio_get_dreq(pio, sm, false));
        dma_channel_configure(rx_ch[j], &c, &out[j * BLK], &pio->rxf[sm], BLK, false);
        dma_channel_set_irq0_enabled(rx_ch[j], true);
    }
    irq_set_exclusive_handler(DMA_IRQ_0, dma_irq);
    irq_set_enabled(DMA_IRQ_0, true);
}

static int tx_start(PIO pio, uint sm) {
    int tx = dma_claim_unused_channel(true);
    dma_channel_config c = dma_channel_get_default_config(tx);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
    channel_config_set_read_increment(&c, true);
    channel_config_set_write_increment(&c, false);
    channel_config_set_dreq(&c, pio_get_dreq(pio, sm, true));
    dma_channel_configure(tx, &c, &pio->txf[sm], in, N, true);  // 整個輸入一次送
    return tx;
}

int main(void) {
    stdio_init_all();
    for (int i = 0; i < N; i++) in[i] = (uint32_t)i * 2654435761u;

    PIO pio = pio0;
    uint sm = pio_claim_unused_sm(pio, true);
    uint off = pio_add_program(pio, &xform_bitrev_program);
    xform_init(pio, sm, off, xform_bitrev_program_get_default_config(off));

    // ---- 原版：一個字一個字 ----
    uint64_t t0 = time_us_64(), ref = 0;
    for (int i = 0; i < N; i++) {
        pio_sm_put_blocking(pio, sm, in[i]);
        ref += work(pio_sm_get_blocking(pio, sm));
    }
    uint64_t t_naive = time_us_64() - t0;
    uint32_t chk_ref = 0;                                    // 原版輸出的 XOR，用來比對整個陣列
    for (int i = 0; i < N; i++) { pio_sm_put_blocking(pio, sm, in[i]); chk_ref ^= pio_sm_get_blocking(pio, sm) * (uint32_t)(i + 1); }

    // ---- 疊加版 ----
    blocks_ready = 0;
    rx_setup(pio, sm);
    multicore_launch_core1(core1_entry);                     // core1 先在旁邊等第 1 塊
    t0 = time_us_64();
    dma_channel_start(rx_ch[0]);                             // 先啟動接收
    int tx = tx_start(pio, sm);
    uint64_t sum = 0;
    for (uint32_t k = 0; k < NBLK; k += 2) {                 // core0：偶數塊
        while (blocks_ready <= k) tight_loop_contents();
        __dmb();
        sum += process_block(&out[k * BLK]);
    }
    multicore_fifo_pop_blocking();
    sum += core1_sum;
    uint64_t t_fast = time_us_64() - t0;

    uint32_t chk = 0;
    for (int i = 0; i < N; i++) chk ^= out[i] * (uint32_t)(i + 1);
    printf("naive %llu us, fast %llu us, sum %s, data %s\n",
           (unsigned long long)t_naive, (unsigned long long)t_fast,
           sum == ref ? "ok" : "MISMATCH", chk == chk_ref ? "ok" : "MISMATCH");

    irq_set_enabled(DMA_IRQ_0, false);
    for (int j = 0; j < 2; j++) { dma_channel_set_irq0_enabled(rx_ch[j], false); dma_channel_unclaim(rx_ch[j]); }
    dma_channel_unclaim(tx);
}
```

**加速**：估計 3–6 倍。總時間 ≈ max(PIO 處理時間, 計算時間 ÷ 2)。若 `work()` 很重，雙核那層接近 2 倍；若 `work()` 很輕，瓶頸變成 PIO 速度，換更短的 PIO 程式（[S04](#s04)）或多個 SM 平行。**實機要自己量**，程式最後會印 naive 和 fast 的微秒數與比對結果。

**改成你的題目只要動三處**：`work()`、PIO 程式名稱（`xform_bitrev_program`）、`N`（必須是 `BLK` 的倍數；不是的話最後一塊用原版處理）。

**坑**：
- 一定要先啟動 RX 再啟動 TX。
- `blocks_ready` 只能由 IRQ 寫；兩核只讀。讀到之後加 `__dmb()` 再讀資料。
- IRQ 處理函式要短，裡面不能 `printf`。
- `multicore_launch_core1` 整個程式只能呼叫一次；如果題目別處已經用了 core1，改成用 FIFO 傳塊編號給它。
- 不要用 DMA chaining 做無縫乒乓：RP2350 的 DMA 有勘誤（SDK 註解寫 RP2350-E5，abort 前要先清掉被串接通道的 enable），用 IRQ 重新啟動比較安全，代價只有每塊幾十個週期。

---

<a id="s18"></a>
## S18 演算法層實測：先換演算法才是輾壓

**題目特徵**：雙層迴圈在找配對、反覆對同一段陣列加總、`n` 超過幾萬。這類題目硬體再怎麼加速也只有幾倍，換演算法是幾百到上千倍。數論、圖論類見 [algorithms.md](algorithms.md)。

**實測**（4 核 Linux VM，GCC 13 `-O2` / `-O3`，每層都比對結果與原版相同，各跑 3 次取最快，兩輪結果的範圍）：

| 問題 | 原版 | 換演算法 | 再疊 | 總加速 |
|---|---|---|---|---|
| 數對計數 a[i]+a[j]=K，n=40000，值域 0..2000 | 雙層迴圈 125–338 ms | 排序 + 雙指標 3.2–4.0 ms | 值域小 → 計數陣列 0.15–0.18 ms | **約 800–2000 倍** |
| 區間和，n=200000，20000 次查詢 | 每次重加 174–349 ms | 前綴和 0.18–0.31 ms | — | **約 1000 倍** |
| popcount 加總，1600 萬個字 | 逐位元 353–390 ms | SWAR 22–33 ms | + 4 執行緒 10–18 ms | **20–37 倍** |
| 矩陣乘法 512×512 | i-j-k 34–54 ms | i-k-j + `restrict` 11.5–15.7 ms | + 4 執行緒 5.7–10 ms | **4–8 倍** |

**一個意外**：popcount 用 4 個累加器在 x86 `-O2` 反而比 1 個慢（30–44 ms 對 22–33 ms），因為編譯器原本的版本已經處理好相依。「多累加器」只在順序執行的 CPU（像 Hazard3）上預期有用，**一定要量**。

**完整可跑的量測程式**（`gcc -O2 -pthread s18_stack_bench.c`；考場可以直接拿來改成自己的題目做對拍與計時）：

```c
// 全層疊加實測（Linux）：每一層都比對結果和原版完全相同
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>

static double now_ms(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e3 + t.tv_nsec / 1e6; }
static uint32_t rs = 2463534242u;
static uint32_t xr(void) { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return rs; }
#define BEST(expr, out) do { double b = 1e18; for (int r_ = 0; r_ < 3; r_++) { double t_ = now_ms(); out = (expr); t_ = now_ms() - t_; if (t_ < b) b = t_; } printf("  %-44s %9.2f ms\n", #expr, b); } while (0)

/* ---------- 1. popcount 加總 ---------- */
#define PN (1 << 24)
static uint32_t pa[PN];
static uint64_t pc_L0(const uint32_t *a, size_t n) { uint64_t s = 0; for (size_t i = 0; i < n; i++) for (int b = 0; b < 32; b++) s += (a[i] >> b) & 1; return s; }
static inline uint32_t swar(uint32_t x) { x = x - ((x >> 1) & 0x55555555u); x = (x & 0x33333333u) + ((x >> 2) & 0x33333333u); return (((x + (x >> 4)) & 0x0F0F0F0Fu) * 0x01010101u) >> 24; }
static uint64_t pc_L1(const uint32_t *a, size_t n) { uint64_t s = 0; for (size_t i = 0; i < n; i++) s += swar(a[i]); return s; }
static uint64_t pc_L2(const uint32_t *a, size_t n) {           // 4 個累加器
    uint64_t s0 = 0, s1 = 0, s2 = 0, s3 = 0; size_t i = 0;
    for (; i + 4 <= n; i += 4) { s0 += swar(a[i]); s1 += swar(a[i+1]); s2 += swar(a[i+2]); s3 += swar(a[i+3]); }
    for (; i < n; i++) s0 += swar(a[i]);
    return s0 + s1 + s2 + s3;
}
typedef struct { const uint32_t *a; size_t n; _Alignas(64) uint64_t r; } pjob;
static void *pc_worker(void *p) { pjob *j = p; j->r = pc_L2(j->a, j->n); return 0; }
static uint64_t pc_L3(const uint32_t *a, size_t n) {           // + 每核一段
    int T = (int)sysconf(_SC_NPROCESSORS_ONLN); if (T > 16) T = 16;
    pthread_t th[16]; pjob jb[16]; size_t per = n / T;
    for (int t = 0; t < T; t++) { jb[t].a = a + t * per; jb[t].n = t == T - 1 ? n - t * per : per; pthread_create(&th[t], 0, pc_worker, &jb[t]); }
    uint64_t s = 0; for (int t = 0; t < T; t++) { pthread_join(th[t], 0); s += jb[t].r; } return s;
}

/* ---------- 2. 矩陣乘法 ---------- */
#define M 512
static float A[M][M], B[M][M], C[M][M];
static float mm_sum(void) { double s = 0; for (int i = 0; i < M; i++) for (int j = 0; j < M; j++) s += C[i][j]; return (float)s; }
static float mm_L0(void) { for (int i = 0; i < M; i++) for (int j = 0; j < M; j++) { float s = 0; for (int k = 0; k < M; k++) s += A[i][k] * B[k][j]; C[i][j] = s; } return mm_sum(); }
static void mm_rows(int r0, int r1) {
    for (int i = r0; i < r1; i++) {
        float *restrict c = C[i]; for (int j = 0; j < M; j++) c[j] = 0;
        for (int k = 0; k < M; k++) { float a = A[i][k]; const float *restrict b = B[k]; for (int j = 0; j < M; j++) c[j] += a * b[j]; }
    }
}
static float mm_L1(void) { mm_rows(0, M); return mm_sum(); }
typedef struct { int r0, r1; } mjob;
static void *mm_worker(void *p) { mjob *j = p; mm_rows(j->r0, j->r1); return 0; }
static float mm_L2(void) {
    int T = (int)sysconf(_SC_NPROCESSORS_ONLN); if (T > 16) T = 16;
    pthread_t th[16]; mjob jb[16];
    for (int t = 0; t < T; t++) { jb[t].r0 = M * t / T; jb[t].r1 = M * (t + 1) / T; pthread_create(&th[t], 0, mm_worker, &jb[t]); }
    for (int t = 0; t < T; t++) pthread_join(th[t], 0);
    return mm_sum();
}

/* ---------- 3. 數對計數：a[i] + a[j] == K ---------- */
#define QN 40000
static int32_t qa[QN], qs[QN];
static long long pair_L0(void) { long long c = 0; for (int i = 0; i < QN; i++) for (int j = i + 1; j < QN; j++) c += qa[i] + qa[j] == 1000; return c; }
static int cmpi(const void *x, const void *y) { int a = *(const int *)x, b = *(const int *)y; return (a > b) - (a < b); }
static long long pair_L1(void) {                                // 排序 + 雙指標 O(n log n)
    memcpy(qs, qa, sizeof qa); qsort(qs, QN, sizeof qs[0], cmpi);
    long long c = 0; int i = 0, j = QN - 1;
    while (i < j) {
        int s = qs[i] + qs[j];
        if (s < 1000) i++; else if (s > 1000) j--;
        else if (qs[i] == qs[j]) { long long m = j - i + 1; c += m * (m - 1) / 2; break; }
        else { int a = qs[i], b = qs[j]; long long ca = 0, cb = 0; while (qs[i] == a) { i++; ca++; } while (qs[j] == b) { j--; cb++; } c += ca * cb; }
    }
    return c;
}
static int cnt[2001];
static long long pair_L2(void) {                                // 值域小 → 計數陣列 O(n + 值域)
    memset(cnt, 0, sizeof cnt); long long c = 0;
    for (int i = 0; i < QN; i++) { int need = 1000 - qa[i]; if (need >= 0 && need <= 2000) c += cnt[need]; cnt[qa[i]]++; }
    return c;
}

/* ---------- 4. 區間和查詢 ---------- */
#define RN 200000
#define RQ 20000
static int32_t ra[RN]; static int64_t pre[RN + 1]; static int ql[RQ], qr[RQ];
static long long rq_L0(void) { long long t = 0; for (int q = 0; q < RQ; q++) { long long s = 0; for (int i = ql[q]; i <= qr[q]; i++) s += ra[i]; t ^= s * (q + 1); } return t; }
static long long rq_L1(void) { pre[0] = 0; for (int i = 0; i < RN; i++) pre[i + 1] = pre[i] + ra[i]; long long t = 0; for (int q = 0; q < RQ; q++) t ^= (pre[qr[q] + 1] - pre[ql[q]]) * (q + 1); return t; }

int main(void) {
    for (int i = 0; i < PN; i++) pa[i] = xr();
    for (int i = 0; i < M; i++) for (int j = 0; j < M; j++) { A[i][j] = (xr() % 100) / 10.0f; B[i][j] = (xr() % 100) / 10.0f; }
    for (int i = 0; i < QN; i++) qa[i] = (int)(xr() % 2001);
    for (int i = 0; i < RN; i++) ra[i] = (int)(xr() % 2000001) - 1000000;
    for (int q = 0; q < RQ; q++) { int a = xr() % RN, b = xr() % RN; ql[q] = a < b ? a : b; qr[q] = a < b ? b : a; }

    uint64_t p0, p1, p2, p3; puts("popcount 加總（16M 個 uint32）");
    BEST(pc_L0(pa, PN), p0); BEST(pc_L1(pa, PN), p1); BEST(pc_L2(pa, PN), p2); BEST(pc_L3(pa, PN), p3);
    printf("  一致：%s\n", p0 == p1 && p1 == p2 && p2 == p3 ? "是" : "否");

    float m0, m1, m2; puts("矩陣乘法 512x512 float");
    BEST(mm_L0(), m0); BEST(mm_L1(), m1); BEST(mm_L2(), m2);
    printf("  總和 %.6g %.6g %.6g（浮點加法順序不同，誤差 < 1e-5 算一致）\n", m0, m1, m2);

    long long c0, c1, c2; puts("數對計數 n=40000，值域 0..2000");
    BEST(pair_L0(), c0); BEST(pair_L1(), c1); BEST(pair_L2(), c2);
    printf("  一致：%s（%lld 對）\n", c0 == c1 && c1 == c2 ? "是" : "否", c0);

    long long r0, r1; puts("區間和 n=200000，20000 次查詢");
    BEST(rq_L0(), r0); BEST(rq_L1(), r1);
    printf("  一致：%s\n", r0 == r1 ? "是" : "否");
}
```

**換演算法速查**：

| 原版長這樣 | 換成 | 複雜度 |
|---|---|---|
| 雙層迴圈找 a[i]+a[j]=K | 排序 + 雙指標；值域小用計數陣列；否則雜湊 | O(n²) → O(n log n) / O(n) |
| 每次查詢重新加總區間 | 前綴和 `pre[r+1] - pre[l]` | O(nQ) → O(n + Q) |
| 二維區間和 | 二維前綴和 | O(nmQ) → O(nm + Q) |
| 每次查詢找最大 / 最小 | 排序後二分，或 sparse table | O(nQ) → O(Q log n) |
| 反覆線性搜尋 | 排序一次 + 二分 | O(nQ) → O((n + Q) log n) |
| 判斷是否出現過 | bitset（值域小）或雜湊 | O(n) → O(1) |
| 重複算同一個子問題 | 記憶化 / DP | 指數 → 多項式 |
| 取模的次方 | 快速冪 | O(e) → O(log e) |
