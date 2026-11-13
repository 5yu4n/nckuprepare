# RP2350 / pico-sdk / RISC-V / ROS 2 上機考速查筆記

> **查證方式**：所有 pico-sdk 名稱都對照 `raspberrypi/pico-sdk` 原始碼（2.3.1，2026-09），並確認 **2.0.0 起就都存在**；C 範例用 SDK 標頭以 clang 對 RISC-V（`rp2350-riscv`）與 Arm（Cortex-M33）兩種目標做過語法檢查；`.pio` 範例用 SDK 內的 `pioasm` 實際組譯過；RISC-V 指令用 clang 以 RP2350 的 `-march` 實際組譯過；pthread / 位元運算範例在 Linux 上編譯執行過；rclpy / rclcpp / ros2 CLI 名稱對照 `ros2/*` 的 `jazzy` 分支原始碼。
> 標 **未查證** 的是沒能對到一手來源的，用之前先在考場文件確認。
>
> **整套檔案**（請全部放在同一個 Gist 或同一個資料夾，連結才會通）：
> - [00-START.md](00-START.md)：**考試當天從這裡開始**：流程、出事怎麼辦、每段貼給小模型要多少 token
> - [solutions.md](solutions.md)：18 種預測題型的完整優化解答，開頭有「題目長這樣 → 跳到哪」與每題的全層疊加配方
> - [prompts.md](prompts.md)：貼給考場小模型的 prompt（通用一份 + 每種題型一份 + 編譯錯誤、找瓶頸、ROS 2 除錯、對拍）
> - [methodology.md](methodology.md)：優化方法論，小模型不可靠時照著一步步硬解
> - [rp2350-riscv.md](rp2350-riscv.md)：Hazard3 RISC-V 冷門知識（判斷板子與架構、可用指令、行內組語、週期計數、從 Arm 搬過來的坑）
> - [build-and-debug.md](build-and-debug.md)：CMake 範本、建置、燒錄、計時與對拍工具、當機診斷
> - [optimization.md](optimization.md)：量測方法、記憶體放置、編譯器、演算法微技巧、DMA 乒乓 / PIO / 雙核心、時脈、pthread、ROS 2
> - [algorithms.md](algorithms.md)：數論、圖論（計數、取模、質數、gcd、最短路、連通、DP）

## 目錄
0. 考場用法（先看）
1. 練習題「transfer engine」↔ pico-sdk DMA 對照
2. DMA
3. PIO
4. 同步與雙核心（spin lock / critical section / mutex / multicore / pthread）
5. 效能優化
6. RISC-V（RV32 / Hazard3）
7. ROS 2 Jazzy（rclpy / rclcpp / CLI）
8. 建置（CMake、pioasm、平台）
9. 讓考場的小模型不亂編：prompt 範本
10. 常見坑總表

---

## 0. 考場用法（先看）

1. 先讀考題給的文件，找出它用的是 **pico-sdk 函式** 還是 **考題自己包的 API**（像練習題的 `engine_*`）。考題自己的 API 永遠優先，這份筆記只當對照。
2. 要叫考場 LLM 寫程式時，**從本筆記複製對應章節的 API 表 + 範本**，貼進第 9 節的 prompt，要求它只能用清單裡的名稱。
3. 小模型寫完，用 `Ctrl+F` 在本筆記搜尋它用的每個函式名；搜不到 = 可能是捏造的，要嘛改成表內名稱，要嘛去考題文件確認。
4. 送 OJ 前：計數單位、對齊、有沒有清 IRQ、有沒有等「收」完（見第 10 節）。
5. 範本裡自己定義的函式（`dma_copy_words`、`dma_pio_echo`、`inc_cs`、`hot_loop`、`asm_sum` 等）**不是 SDK API**，只能照抄整段用；API 表裡列的才是 SDK 本身的名稱。

---

## 1. 練習題 transfer engine ↔ pico-sdk DMA

練習題四拍：**描述 → 啟動 → 等待 → 取用結果**，真硬體完全一樣。

| 練習題 `engine.h` | pico-sdk `hardware/dma.h` | 備註 |
|---|---|---|
| `engine_claim()` → `-1` 表示沒了 | `dma_claim_unused_channel(bool required)` → `int` | `required=true` 沒通道就 panic；`false` 回 `-1` |
| `engine_job_t` | `dma_channel_config c = dma_channel_get_default_config(ch);` | 2.2.0 起也叫 `dma_channel_config_t`，舊名照樣能用 |
| `.size = XFER_8/16/32` | `channel_config_set_transfer_data_size(&c, DMA_SIZE_8/16/32)` | 列舉值是 0/1/2，**不是** 1/2/4 |
| `.src_increment` | `channel_config_set_read_increment(&c, bool)` | 預設 `true` |
| `.dst_increment` | `channel_config_set_write_increment(&c, bool)` | 預設 `false`！複製要自己設 `true` |
| `.count`（筆數） | `dma_channel_configure(..., transfer_count, ...)` | **筆數，不是 bytes** |
| `engine_configure()` | `dma_channel_configure(ch, &c, write_addr, read_addr, count, trigger)` | **寫入端在前**，跟 memcpy 一樣 dst 在前 |
| `engine_start()` | `dma_channel_start(ch)` 或 configure 時 `trigger=true` | 多通道同時：`dma_start_channel_mask(mask)` |
| `engine_busy()` | `dma_channel_is_busy(ch)` | |
| `engine_wait()` | `dma_channel_wait_for_finish_blocking(ch)` | 內含 compiler barrier |
| `engine_release()` | `dma_channel_unclaim(ch)` | 可先 `dma_channel_cleanup(ch)`（停 IRQ、abort、清旗標） |
| 4 個通道 | RP2350 有 **16** 個（`NUM_DMA_CHANNELS`） | RP2040 是 12 個 |
| size 不對 / 未對齊會 die | 硬體不會報錯，位址低位直接被忽略或資料錯亂 | 自己保證對齊 |

`count` 填成 bytes 的後果：搬 4 倍的量、寫爆後面的記憶體，**沒有任何錯誤訊息**。
正確：`count = bytes / 每筆大小`，例如 `N * sizeof(uint32_t)` 個 bytes 用 `DMA_SIZE_32` 時 count = `N`。

---

## 2. DMA（`#include "hardware/dma.h"`，CMake 連結 `hardware_dma`）

### 2.1 API 表

| 類別 | 函式 / 巨集 | 說明 |
|---|---|---|
| 通道 | `int dma_claim_unused_channel(bool required)` | 取一個空通道 |
| | `void dma_channel_claim(uint ch)` / `void dma_channel_unclaim(uint ch)` | 指定通道 / 歸還 |
| | `bool dma_channel_is_claimed(uint ch)` | |
| | `void dma_channel_cleanup(uint ch)` | 停 IRQ、abort、清 pending |
| 設定 | `dma_channel_config dma_channel_get_default_config(uint ch)` | 預設：讀遞增、寫不遞增、DREQ_FORCE（全速）、chain_to=自己（不串）、32-bit、無 ring、無 bswap、enable |
| | `channel_config_set_transfer_data_size(&c, DMA_SIZE_8 / DMA_SIZE_16 / DMA_SIZE_32)` | 位址每筆前進 1/2/4 |
| | `channel_config_set_read_increment(&c, bool)` / `channel_config_set_write_increment(&c, bool)` | |
| | `channel_config_set_dreq(&c, uint dreq)` | 節奏來源，見 2.2 |
| | `channel_config_set_chain_to(&c, uint other_ch)` | 做完自動觸發另一通道；設成自己 = 關閉 |
| | `channel_config_set_ring(&c, bool write, uint size_bits)` | `write=true` 套用在寫端；只有低 `size_bits` 位會變，繞 `1<<size_bits` bytes；buffer 要對齊該大小 |
| | `channel_config_set_bswap(&c, bool)` | 位元組反轉 |
| | `channel_config_set_irq_quiet(&c, bool)` | 完成時不發 IRQ |
| | `channel_config_set_high_priority(&c, bool)` | |
| | `channel_config_set_enable(&c, bool)` | |
| | `channel_config_set_read_address_update_type(&c, t)` / `..._write_...` | **RP2350 才有、SDK 2.2.0+**：`DMA_ADDRESS_UPDATE_NONE / _INCREMENT / _INCREMENT_BY_TWO / _DECREMENT` |
| 啟動 | `dma_channel_configure(ch, &c, write_addr, read_addr, count, bool trigger)` | 一次設好全部 |
| | `dma_channel_start(ch)` / `dma_start_channel_mask(uint32_t mask)` | |
| | `dma_channel_set_read_addr(ch, addr, bool trigger)` / `dma_channel_set_write_addr(...)` | 重新觸發常用 |
| | `dma_channel_set_trans_count(ch, count, bool trigger)` | |
| | `dma_channel_set_config(ch, &c, bool trigger)` | |
| | `dma_channel_transfer_from_buffer_now(ch, read_addr, count)` / `dma_channel_transfer_to_buffer_now(ch, write_addr, count)` | 只改一端位址並立刻開始 |
| 等待/停止 | `bool dma_channel_is_busy(ch)` / `void dma_channel_wait_for_finish_blocking(ch)` | |
| | `void dma_channel_abort(ch)` | RP2350 勘誤 E5：abort 前先關掉該通道與被串通道的 enable |
| 中斷 | `dma_channel_set_irq0_enabled(ch, bool)` / `dma_channel_set_irq1_enabled(ch, bool)` | |
| | `dma_irqn_set_channel_enabled(irq_index, ch, bool)` | `irq_index` 0..3（RP2350 有 4 條 DMA IRQ） |
| | `dma_channel_get_irq0_status(ch)` / `dma_channel_acknowledge_irq0(ch)` | 也可直接寫 `dma_hw->ints0 = 1u << ch;` |
| | `dma_irqn_get_channel_status(irq_index, ch)` / `dma_irqn_acknowledge_channel(irq_index, ch)` | |
| | IRQ 號碼：`DMA_IRQ_0` … `DMA_IRQ_3` | 搭配 `irq_set_exclusive_handler(num, fn)`、`irq_set_enabled(num, true)`（`hardware/irq.h`） |
| 檢查碼 | `dma_sniffer_enable(ch, mode, bool force_channel_enable)`、`dma_sniffer_set_data_accumulator(seed)`、`dma_sniffer_get_data_accumulator()` | mode 0x0 CRC-32、0x2 CRC-16-CCITT、0xe XOR、0xf 加總 |
| 計時器 | `dma_claim_unused_timer(bool)`、`dma_timer_set_fraction(timer, num, den)`、DREQ `DREQ_DMA_TIMER0…` | 固定頻率搬運 |
| 2.2.0+ | `dma_encode_transfer_count(n)` | RP2350 的 count 只用低 28 位（上限 2^28-1），高 4 位是模式；舊版 SDK 沒這函式，直接傳數字即可 |

### 2.2 DREQ（節奏）

| 用途 | 寫法 |
|---|---|
| 記憶體 ↔ 記憶體（全速） | 不設，預設 `DREQ_FORCE` |
| 寫進 PIO TX FIFO | `channel_config_set_dreq(&c, pio_get_dreq(pio, sm, true));` 寫端 `&pio->txf[sm]`、`write_increment=false` |
| 從 PIO RX FIFO 讀 | `channel_config_set_dreq(&c, pio_get_dreq(pio, sm, false));` 讀端 `&pio->rxf[sm]`、`read_increment=false` |
| 常數名稱 | `DREQ_PIO0_TX0`…`DREQ_PIO2_RX3`、`DREQ_SPI0_TX`、`DREQ_UART0_TX`、`DREQ_ADC`、`DREQ_FORCE` |

### 2.3 範本（已語法檢查）

下面 `dma_copy_words` 等函式是範本自訂的包裝，裡面呼叫的才是 SDK API。

```c
#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/dma.h"
#include "hardware/pio.h"
#include "hardware/irq.h"
#include "echo.pio.h"

#define N 1024
static uint32_t src[N], dst[N];

// ---------- A. memcpy：描述 -> 啟動 -> 等待 -> 歸還 ----------
void dma_copy_words(uint32_t *to, const uint32_t *from, uint count_words) {
    int ch = dma_claim_unused_channel(true);                 // true: 沒通道就 panic
    dma_channel_config c = dma_channel_get_default_config(ch);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_32);  // 每筆 4 bytes
    channel_config_set_read_increment(&c, true);
    channel_config_set_write_increment(&c, true);
    dma_channel_configure(ch, &c,
                          to,            // write_addr（注意：寫入端在前）
                          from,          // read_addr
                          count_words,   // 筆數，不是 bytes！
                          true);         // 立刻開始
    dma_channel_wait_for_finish_blocking(ch);
    dma_channel_unclaim(ch);
}

// ---------- B. fill：只差 read_increment=false ----------
void dma_fill_words(uint32_t *to, uint32_t value, uint count_words) {
    static uint32_t pattern;                                 // DMA 讀的位址要在傳輸期間有效
    pattern = value;
    int ch = dma_claim_unused_channel(true);
    dma_channel_config c = dma_channel_get_default_config(ch);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
    channel_config_set_read_increment(&c, false);            // 一直讀同一個位址
    channel_config_set_write_increment(&c, true);
    dma_channel_configure(ch, &c, to, &pattern, count_words, true);
    dma_channel_wait_for_finish_blocking(ch);
    dma_channel_unclaim(ch);
}

// ---------- C. 非阻塞：啟動後 CPU 做別的事 ----------
void dma_async_demo(void) {
    int ch = dma_claim_unused_channel(true);
    dma_channel_config c = dma_channel_get_default_config(ch);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
    channel_config_set_read_increment(&c, true);
    channel_config_set_write_increment(&c, true);
    dma_channel_configure(ch, &c, dst, src, N, false);       // false: 先不啟動
    dma_channel_start(ch);
    uint32_t spins = 0;
    while (dma_channel_is_busy(ch)) spins++;                 // 這裡可做其他工作
    dma_channel_unclaim(ch);
    printf("spins=%lu\n", (unsigned long)spins);
}

// ---------- D. 完成中斷 ----------
static int irq_ch;
static volatile bool irq_done;
static void dma_irq_handler(void) {
    if (dma_channel_get_irq0_status(irq_ch)) {
        dma_channel_acknowledge_irq0(irq_ch);                // 一定要清，不然會一直進來
        irq_done = true;
    }
}
void dma_irq_demo(void) {
    irq_ch = dma_claim_unused_channel(true);
    dma_channel_config c = dma_channel_get_default_config(irq_ch);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
    channel_config_set_read_increment(&c, true);
    channel_config_set_write_increment(&c, true);
    dma_channel_configure(irq_ch, &c, dst, src, N, false);
    dma_channel_set_irq0_enabled(irq_ch, true);
    irq_set_exclusive_handler(DMA_IRQ_0, dma_irq_handler);
    irq_set_enabled(DMA_IRQ_0, true);
    irq_done = false;
    dma_channel_start(irq_ch);
    while (!irq_done) tight_loop_contents();
    dma_channel_set_irq0_enabled(irq_ch, false);
    dma_channel_unclaim(irq_ch);
}

// ---------- E. DMA <-> PIO（用 DREQ 控制節奏）----------
void dma_pio_echo(PIO pio, uint sm, const uint32_t *in, uint32_t *out, uint n) {
    int tx = dma_claim_unused_channel(true);
    int rx = dma_claim_unused_channel(true);

    dma_channel_config ct = dma_channel_get_default_config(tx);
    channel_config_set_transfer_data_size(&ct, DMA_SIZE_32);
    channel_config_set_read_increment(&ct, true);
    channel_config_set_write_increment(&ct, false);          // 一直寫同一個 FIFO 位址
    channel_config_set_dreq(&ct, pio_get_dreq(pio, sm, true));   // TX FIFO 有空才搬
    dma_channel_configure(tx, &ct, &pio->txf[sm], in, n, false);

    dma_channel_config cr = dma_channel_get_default_config(rx);
    channel_config_set_transfer_data_size(&cr, DMA_SIZE_32);
    channel_config_set_read_increment(&cr, false);           // 一直讀同一個 FIFO 位址
    channel_config_set_write_increment(&cr, true);
    channel_config_set_dreq(&cr, pio_get_dreq(pio, sm, false));  // RX FIFO 有資料才搬
    dma_channel_configure(rx, &cr, out, &pio->rxf[sm], n, false);

    dma_start_channel_mask((1u << tx) | (1u << rx));         // 同時啟動
    dma_channel_wait_for_finish_blocking(rx);                // 等「收」完，不是等送完
    dma_channel_unclaim(tx);
    dma_channel_unclaim(rx);
}

// ---------- F. ring buffer：寫入端在 256 bytes 內繞圈 ----------
static uint32_t ring[64] __attribute__((aligned(256)));    // 對齊 = ring 大小
void dma_ring_demo(void) {
    int ch = dma_claim_unused_channel(true);
    dma_channel_config c = dma_channel_get_default_config(ch);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
    channel_config_set_read_increment(&c, true);
    channel_config_set_write_increment(&c, true);
    channel_config_set_ring(&c, true, 8);                    // true=寫端, 2^8=256 bytes
    dma_channel_configure(ch, &c, ring, src, N, true);
    dma_channel_wait_for_finish_blocking(ch);
    dma_channel_unclaim(ch);
}

// ---------- G. chain：A 做完自動觸發 B ----------
void dma_chain_demo(void) {
    int a = dma_claim_unused_channel(true);
    int b = dma_claim_unused_channel(true);
    dma_channel_config cb = dma_channel_get_default_config(b);
    channel_config_set_write_increment(&cb, true);
    dma_channel_configure(b, &cb, dst + N / 2, src + N / 2, N / 2, false);
    dma_channel_config ca = dma_channel_get_default_config(a);
    channel_config_set_write_increment(&ca, true);
    channel_config_set_chain_to(&ca, b);                     // 預設 chain_to = 自己 = 不串
    dma_channel_configure(a, &ca, dst, src, N / 2, true);
    dma_channel_wait_for_finish_blocking(a);
    dma_channel_wait_for_finish_blocking(b);
    dma_channel_unclaim(a);
    dma_channel_unclaim(b);
}

int main(void) {
    stdio_init_all();
    for (int i = 0; i < N; i++) src[i] = i * 2654435761u;
    dma_copy_words(dst, src, N);
    dma_fill_words(dst, 0xA5A5A5A5u, N);
    dma_async_demo();
    dma_irq_demo();
    dma_ring_demo();
    dma_chain_demo();

    PIO pio = pio0;
    uint offset = pio_add_program(pio, &echo_program);
    int sm = pio_claim_unused_sm(pio, true);
    echo_program_init(pio, sm, offset);
    dma_pio_echo(pio, sm, src, dst, N);

    // 另一種寫法：SDK 2.x 自動找空的 PIO+SM 並載入
    PIO p2; uint sm2, off2;
    bool ok = pio_claim_free_sm_and_add_program(&echo_program, &p2, &sm2, &off2);
    if (ok) {
        echo_program_init(p2, sm2, off2);
        pio_sm_put_blocking(p2, sm2, 123);
        printf("%lu\n", (unsigned long)pio_sm_get_blocking(p2, sm2));
        pio_remove_program_and_unclaim_sm(&echo_program, p2, sm2, off2);
    }
    return 0;
}
```

---

## 3. PIO（`#include "hardware/pio.h"`，CMake 連結 `hardware_pio`）

### 3.1 架構速記

| 項目 | RP2350 | 備註 |
|---|---|---|
| PIO 區塊 | 3 個：`pio0` `pio1` `pio2` | RP2040 只有 2 個 |
| 每區塊狀態機 (SM) | 4 個 | 共 12 個 |
| 指令記憶體 | 每區塊 32 條，4 個 SM 共用 | `PIO_INSTRUCTION_COUNT` |
| 暫存器 | X、Y（32-bit 暫存）、OSR（輸出移位）、ISR（輸入移位） | 沒有一般記憶體 |
| FIFO | TX 4 層、RX 4 層；join 後單向 8 層 | |
| 時脈 | 每條指令 1 週期（＋delay），可分頻 `clk_sys / div`（16.8 定點） | RP2350 預設 `clk_sys` 150 MHz |
| PIO 版本 | `.pio_version 0` = RP2040 指令集；`.pio_version 1` = RP2350 新指令 | |

資料流：`CPU → TX FIFO → (pull) → OSR → (out) → 腳位/X/Y… → (in) → ISR → (push) → RX FIFO → CPU`

### 3.2 指令集（pioasm 語法，對照 SDK `tools/pioasm/parser.yy`）

每條指令後面都可加 `[delay]` 與 `side <值>`。delay 與 side-set 共用 5 bit：沒有 side-set 時 delay 0–31；`.side_set n` 用掉 n bit；`.side_set n opt` 再多用 1 bit。

| 指令 | 語法 | 作用 / 重點 |
|---|---|---|
| `jmp` | `jmp [cond] label` | cond：（空）永遠、`!x`（x==0）、`x--`（x!=0 時跳，然後 x 減 1）、`!y`、`y--`、`x!=y`、`pin`（jmp pin 為 1）、`!osre`（OSR 還沒空） |
| `wait` | `wait <0/1> gpio <n>` / `wait <0/1> pin <n>` / `wait <0/1> irq <n> [rel]` | 停住直到條件成立；`gpio` 是絕對編號，`pin` 相對 IN base；省略 0/1 時預設 1 |
| `in` | `in <src>, <1-32>` | 從 src 移入 ISR。src：`pins` `x` `y` `null` `isr` `osr` |
| `out` | `out <dst>, <1-32>` | 從 OSR 移出到 dst。dst：`pins` `x` `y` `null` `pindirs` `pc` `isr` `exec` |
| `push` | `push [iffull] [block\|noblock]` | ISR → RX FIFO 並清空 ISR；預設 `block`（RX 滿就停） |
| `pull` | `pull [ifempty] [block\|noblock]` | TX FIFO → OSR；預設 `block`；`noblock` 且 FIFO 空時 OSR ← X（pico-examples `pio/pwm/pwm.pio` 註解確認） |
| `mov` | `mov <dst>, [op]<src>` | dst：`pins` `x` `y` `exec` `pc` `isr` `osr`；src：`pins` `x` `y` `null` `status` `isr` `osr`；op：`!` 或 `~`（反相）、`::`（位元反轉） |
| `irq` | `irq [set\|nowait\|wait\|clear] <0-7> [rel]` | 設/清 PIO 內部 IRQ 旗標；`wait` 會等到旗標被清掉 |
| `set` | `set <dst>, <0-31>` | dst：`pins` `x` `y` `pindirs`（最多 5 個 set 腳位） |
| `nop` | `nop` | 等於 `mov y, y`，常用來只做 side-set 或 delay |

**RP2350（`.pio_version 1`）才有：**

| 新語法 | 說明 |
|---|---|
| `mov pindirs, <src>` | mov 可寫腳位方向 |
| `mov rxfifo[y], isr` / `mov rxfifo[0-3], isr` | 把 ISR 寫進 RX FIFO 指定格；**來源只能是 isr**，需 `.fifo txput` 或 `.fifo putget`。只有 4 格（`rxf_putget[4][4]`），用 0–3；pioasm 雖收 0–7，4–7 的行為未查證 |
| `mov osr, rxfifo[y]` / `mov osr, rxfifo[0-3]` | 從 RX FIFO 指定格讀；**目的只能是 osr**，需 `.fifo txget` 或 `.fifo putget` |
| `irq prev <n>` / `irq next <n>`、`wait 1 irq prev <n>` / `wait 1 irq next <n>` | 跨 PIO 區塊的 IRQ（不能加 `rel`） |
| `wait <0/1> jmppin` / `wait <0/1> jmppin + <n>` | 等 jmp pin（或其後第 n 腳） |
| `.mov_status irq [prev\|next] set <n>` | `mov x, status` 的來源可選 IRQ 旗標 |
| CPU 端 | `pio->rxf_putget[sm][0..3]` 直接讀寫那 4 格，**只在** `.fifo txput`（SM 寫、CPU 讀）或 `.fifo txget`（CPU 寫、SM 讀）時可用；`.fifo putget` 時 CPU 完全碰不到（SDK `regs/pio.h` 說明：PUT xor GET 才開放） |

### 3.3 指示詞（directives）

| 指示詞 | 說明 |
|---|---|
| `.program name` | 產生 `name_program`、`name_program_get_default_config(offset)`、`name_wrap_target`、`name_wrap` |
| `.wrap_target` / `.wrap` | 免費迴圈（不花指令） |
| `.side_set <n> [opt] [pindirs]` | side-set 位元數 |
| `.origin <addr>` | 固定載入位址 |
| `.pio_version 0\|1`（或 `RP2040` / `RP2350`） | |
| `.fifo txrx\|tx\|rx\|txput\|txget\|putget` | 後三個是 RP2350 |
| `.mov_status txfifo < n` / `rxfifo < n` | `mov x, status` 的判斷條件 |
| `.in <pins> [left\|right] [auto\|manual] [threshold]` / `.out ...` / `.set <n>` | 2.x pioasm 支援；不確定時改在 C 端用 `sm_config_set_in_shift/out_shift` 設 |
| `.clock_div <n>` | |
| `.word <16-bit>` | 塞原始編碼 |
| `% c-sdk { ... %}` | 這段 C 會原樣放進產生的 `.pio.h`，用來寫 `xxx_program_init()` |
| `public label:` | 標籤匯出成 `name_offset_label` |

### 3.4 C 端 API

| 類別 | 函式 | 說明 |
|---|---|---|
| 載入 | `int pio_add_program(PIO pio, const pio_program_t *prog)` | 回傳 offset |
| | `bool pio_can_add_program(pio, prog)`、`pio_add_program_at_offset(pio, prog, off)`、`pio_remove_program(pio, prog, off)`、`pio_clear_instruction_memory(pio)` | |
| | `bool pio_claim_free_sm_and_add_program(prog, &pio, &sm, &offset)` | 一次找好 PIO+SM+載入（2.0+） |
| | `pio_claim_free_sm_and_add_program_for_gpio_range(prog, &pio, &sm, &off, gpio_base, gpio_count, set_gpio_base)` | 要用 GPIO ≥ 32 時 |
| | `pio_remove_program_and_unclaim_sm(prog, pio, sm, offset)` | |
| SM | `int pio_claim_unused_sm(PIO pio, bool required)`、`pio_sm_claim(pio, sm)`、`pio_sm_unclaim(pio, sm)` | |
| | `int pio_sm_init(pio, sm, initial_pc, &config)` | 套設定、跳到 initial_pc（**不會**啟動） |
| | `pio_sm_set_enabled(pio, sm, bool)`、`pio_set_sm_mask_enabled(pio, mask, bool)`、`pio_enable_sm_mask_in_sync(pio, mask)` | |
| | `pio_sm_restart(pio, sm)`、`pio_sm_clear_fifos(pio, sm)`、`pio_sm_drain_tx_fifo(pio, sm)` | |
| | `pio_sm_exec(pio, sm, instr)`、`pio_sm_exec_wait_blocking(pio, sm, instr)`、`pio_sm_get_pc(pio, sm)` | instr 用 `pio_encode_jmp(addr)`、`pio_encode_set(pio_x, v)`、`pio_encode_pull(false, true)`、`pio_encode_out(pio_x, 32)` 等產生 |
| FIFO | `pio_sm_put_blocking(pio, sm, u32)` / `uint32_t pio_sm_get_blocking(pio, sm)` | |
| | `pio_sm_put(pio, sm, u32)` / `pio_sm_get(pio, sm)`（不檢查） | |
| | `pio_sm_is_tx_fifo_full/empty`、`pio_sm_is_rx_fifo_full/empty`、`pio_sm_get_tx_fifo_level`、`pio_sm_get_rx_fifo_level` | |
| | 直接存取：`pio->txf[sm]`（寫）、`pio->rxf[sm]`（讀） | DMA 用這兩個位址 |
| 腳位 | `pio_gpio_init(pio, pin)` | 把 GPIO 功能切到這個 PIO |
| | `pio_sm_set_consecutive_pindirs(pio, sm, base, count, bool is_out)` | |
| | `pio_sm_set_pins(pio, sm, values)`、`pio_sm_set_pins_with_mask(pio, sm, values, mask)`、`pio_sm_set_pindirs_with_mask(pio, sm, dirs, mask)` | |
| 中斷 | `pio_set_irq0_source_enabled(pio, pis_interrupt0 + n, true)`、`pio_interrupt_get(pio, n)`、`pio_interrupt_clear(pio, n)` | 系統 IRQ：`PIO0_IRQ_0` `PIO0_IRQ_1` … `PIO2_IRQ_1`，或 `pio_get_irq_num(pio, 0/1)` |
| DREQ | `uint pio_get_dreq(PIO pio, uint sm, bool is_tx)` | |
| 其他 | `pio_get_index(pio)`、`pio_get_instance(i)`、`pio_get_gpio_base(pio)` | |

**`pio_sm_config` 設定（`sm_config_*`，在 `pio_sm_init` 之前呼叫）：**

| 函式 | 說明 |
|---|---|
| `pio_get_default_sm_config()` | 預設：clkdiv 1、wrap 0..31、in/out 都 shift right、不 auto、門檻 32 |
| `sm_config_set_out_pins(&c, base, count)` | `out pins` / `mov pins` 用 |
| `sm_config_set_set_pins(&c, base, count)` | `set pins` 用（count ≤ 5） |
| `sm_config_set_in_pins(&c, base)`（2.x 另有 `sm_config_set_in_pin_count`） | `in pins` / `wait pin` 用 |
| `sm_config_set_sideset_pins(&c, base)` | side-set 腳位起點 |
| `sm_config_set_sideset(&c, bit_count, optional, pindirs)` | 通常由 `.side_set` 自動設好 |
| `sm_config_set_jmp_pin(&c, pin)` | `jmp pin` / `wait jmppin` 用 |
| `sm_config_set_clkdiv(&c, float div)` / `sm_config_set_clkdiv_int_frac8(&c, int, frac8)` | 例：`clock_get_hz(clk_sys) / 目標頻率` |
| `sm_config_set_wrap(&c, wrap_target, wrap)` | 由 `xxx_program_get_default_config` 自動設 |
| `sm_config_set_out_shift(&c, bool shift_right, bool autopull, uint pull_threshold)` | autopull：OSR 移出達門檻就自動補 |
| `sm_config_set_in_shift(&c, bool shift_right, bool autopush, uint push_threshold)` | autopush：ISR 移入達門檻就自動 push |
| `sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_NONE/_TX/_RX)`；RP2350 另有 `_TXGET/_TXPUT/_PUTGET` | |
| `sm_config_set_mov_status(&c, STATUS_TX_LESSTHAN/STATUS_RX_LESSTHAN, n)` | RP2350 另有 `STATUS_IRQ_SET` |
| `sm_config_set_out_special(&c, sticky, has_enable_pin, enable_bit_index)` | |

### 3.5 PIO 範本（全部用 pioasm 組譯過）

**echo（練習題那支，加上 init）**
```pio
.pio_version 0
.program echo
; CPU -> TX FIFO -> OSR -(out)-> X -(in)-> ISR -(push)-> RX FIFO -> CPU
.wrap_target
    out x, 32       ; autopull 開著：OSR 空了自動從 TX FIFO 補
    in  x, 32
    push            ; 也可改用 autopush，這行就能刪
.wrap

% c-sdk {
static inline void echo_program_init(PIO pio, uint sm, uint offset) {
    pio_sm_config c = echo_program_get_default_config(offset);
    sm_config_set_out_shift(&c, false, true, 32);   // shift_left, autopull, 門檻 32
    sm_config_set_in_shift(&c, false, false, 32);   // shift_left, 不 autopush
    pio_sm_init(pio, sm, offset, &c);
    pio_sm_set_enabled(pio, sm, true);
}
%}
```

**輸出 1 bit + delay 迴圈**
```pio
.pio_version 0
.program blink
; 每個 32-bit word 的低位決定輸出，[n] 為額外延遲 n 個週期
.wrap_target
    pull block          ; 等 CPU 放資料
    out pins, 1         ; 輸出 1 bit 到 OUT 腳位
    set x, 31 [7]       ; X = 31，再多等 7 個週期
lp:
    jmp x-- lp [7]      ; 迴圈 32 次，每次 8 週期
.wrap

% c-sdk {
static inline void blink_program_init(PIO pio, uint sm, uint offset, uint pin, float clkdiv) {
    pio_sm_config c = blink_program_get_default_config(offset);
    sm_config_set_out_pins(&c, pin, 1);
    sm_config_set_clkdiv(&c, clkdiv);
    pio_gpio_init(pio, pin);
    pio_sm_set_consecutive_pindirs(pio, sm, pin, 1, true);
    pio_sm_init(pio, sm, offset, &c);
    pio_sm_set_enabled(pio, sm, true);
}
%}
```

**side-set 當時脈（SPI 只送）**
```pio
.pio_version 0
.program spi_tx
.side_set 1             ; 1 個 side-set 腳位 = SCK，吃掉 delay 欄位 1 bit（剩 0..15）
; OUT 腳位 = MOSI。autopull，MSB 先送（shift left）
.wrap_target
    out pins, 1   side 0 [1]
    nop           side 1 [1]
.wrap

% c-sdk {
static inline void spi_tx_program_init(PIO pio, uint sm, uint offset, uint mosi, uint sck, float clkdiv) {
    pio_sm_config c = spi_tx_program_get_default_config(offset);
    sm_config_set_out_pins(&c, mosi, 1);
    sm_config_set_sideset_pins(&c, sck);
    sm_config_set_out_shift(&c, false, true, 8);    // 每 8 bit 自動補（送 byte 時資料要放在 bit31..24）
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX);  // 只送不收：TX FIFO 變 8 層
    sm_config_set_clkdiv(&c, clkdiv);
    pio_gpio_init(pio, mosi);
    pio_gpio_init(pio, sck);
    pio_sm_set_consecutive_pindirs(pio, sm, mosi, 1, true);
    pio_sm_set_consecutive_pindirs(pio, sm, sck, 1, true);
    pio_sm_init(pio, sm, offset, &c);
    pio_sm_set_enabled(pio, sm, true);
}
%}
```
送 8-bit 時：shift left 會從 bit31 開始送，所以要 `pio_sm_put_blocking(pio, sm, (uint32_t)byte << 24)`。

**純運算：數 1 的個數（x/y 迴圈、`!osre`）**
```pio
.pio_version 0
.program popcount
; 計算每個 word 裡 1 的個數（示範 x/y 迴圈與 !osre）。結果 = ~y
.wrap_target
    pull block
    mov y, ~null        ; y = 0xFFFFFFFF（當作 -1 起算的計數器）
bit:
    out x, 1            ; 取 1 bit
    jmp !x skip
    jmp y-- skip        ; y-- (y 不會為 0，所以永遠跳到 skip)
skip:
    jmp !osre bit       ; OSR 還有 bit 就繼續
    mov isr, ~y         ; ~y = 1 的個數
    push
.wrap
```

**RP2350 新指令示範**（依 SDK 2.3.1 pioasm 原始碼與 `pio.h` 修正：`putget` 模式 CPU 完全碰不到那 4 格，所以 CPU 要讀就用 `txput`、要寫就用 `txget`；修正後這版沒重新組譯）
```pio
.pio_version 1          ; 放在第一個 .program 前，後面每個程式都算版本 1
.program put_demo
.fifo txput             ; RX FIFO 變 4 格：SM 用 mov rxfifo[] 寫、CPU 讀 rxf_putget（不能再用一般 RX FIFO 讀）
    pull block
    mov isr, osr
    mov rxfifo[0], isr  ; CPU 讀 pio->rxf_putget[sm][0]

.program get_demo
.fifo txget             ; RX FIFO 變 4 格：CPU 寫 rxf_putget、SM 用 mov osr, rxfifo[] 讀
    pull block          ; 等 CPU 說「寫好了」
    mov osr, rxfifo[1]  ; 讀 CPU 先寫進 rxf_putget[sm][1] 的值
    mov x, osr
    irq next 0          ; 觸發「下一個」PIO block 的 IRQ 0
    wait 1 jmppin       ; 等 JMP pin 為 1
```

**C 端使用（已語法檢查；最後 `txput`/`txget` 那段是 11-13 晚上修正的，沒重新編譯）**
```c
#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "popcount.pio.h"
#include "rp2350_regs.pio.h"

int main(void) {
    stdio_init_all();
    PIO pio = pio1;
    uint off = pio_add_program(pio, &popcount_program);
    uint sm = (uint)pio_claim_unused_sm(pio, true);
    pio_sm_config c = popcount_program_get_default_config(off);
    // 預設：in/out 都 shift right、不 autopush/autopull、門檻 32、clkdiv 1
    pio_sm_init(pio, sm, off, &c);
    pio_sm_set_enabled(pio, sm, true);

    pio_sm_put_blocking(pio, sm, 0xF0F0F0F0u);
    printf("popcount=%lu\n", (unsigned long)pio_sm_get_blocking(pio, sm));   // 16

    // 狀態查詢 / 除錯
    printf("pc=%u txlvl=%u rxlvl=%u\n", pio_sm_get_pc(pio, sm),
           pio_sm_get_tx_fifo_level(pio, sm), pio_sm_get_rx_fifo_level(pio, sm));
    pio_sm_set_enabled(pio, sm, false);
    pio_sm_clear_fifos(pio, sm);
    pio_sm_restart(pio, sm);
    pio_sm_exec(pio, sm, pio_encode_jmp(off));          // 強制跳回程式開頭（立即執行一條指令）
    pio_sm_unclaim(pio, sm);
    pio_remove_program(pio, &popcount_program, off);

    // RP2350：.fifo txput → CPU 讀 rxf_putget；.fifo txget → CPU 寫 rxf_putget
    uint offp = pio_add_program(pio2, &put_demo_program);
    pio_sm_config cp = put_demo_program_get_default_config(offp);   // 已含 sm_config_set_fifo_join(TXPUT)
    pio_sm_init(pio2, 0, offp, &cp);
    pio_sm_set_enabled(pio2, 0, true);
    pio_sm_put_blocking(pio2, 0, 42);
    sleep_us(10);                                // 等 SM 跑完那三條指令
    printf("%lu\n", (unsigned long)pio2->rxf_putget[0][0]);   // 42

    uint offg = pio_add_program(pio2, &get_demo_program);
    pio_sm_config cg = get_demo_program_get_default_config(offg);   // 已含 sm_config_set_fifo_join(TXGET)
    pio_sm_init(pio2, 1, offg, &cg);
    pio2->rxf_putget[1][1] = 0x1234;             // 給 SM1 讀
    pio_sm_set_enabled(pio2, 1, true);
    pio_sm_put_blocking(pio2, 1, 0);             // 叫 SM1 去讀
    return 0;
}
```

---

## 4. 同步與雙核心

### 4.1 怎麼選

| 情境 | 用什麼 | 標頭 |
|---|---|---|
| 兩核心共用變數、時間很短 | `critical_section_t`（最省事） | `pico/sync.h` |
| 同上，要自己管 lock 編號 | 硬體 spin lock：`spin_lock_blocking` / `spin_unlock` | `hardware/sync.h` |
| 主程式 vs 自己核心的 ISR | `save_and_disable_interrupts()` / `restore_interrupts(s)` | `hardware/sync.h` |
| 持有時間長、可等待 | `mutex_t`（不能在 IRQ 用） | `pico/sync.h` |
| 計數資源 | `semaphore_t`：`sem_init`、`sem_acquire_blocking`、`sem_release` | `pico/sync.h` |
| 核心間傳 32-bit 訊息 | `multicore_fifo_push_blocking` / `multicore_fifo_pop_blocking` | `pico/multicore.h` |
| Linux / 練習題 | `pthread_spin_*`、`pthread_mutex_*`、C11 `stdatomic.h` | |

原則（練習題 2-3 的重點）：**鎖越短越好**，只鎖讀-改-寫那幾行，運算放鎖外。

### 4.2 API 表

| 名稱 | 說明 |
|---|---|
| `int spin_lock_claim_unused(bool required)` | 取 lock 編號（RP2350 共 32 個） |
| `spin_lock_t *spin_lock_init(uint lock_num)` | 初始化並回傳指標 |
| `spin_lock_t *spin_lock_instance(uint lock_num)` | 只取指標 |
| `uint32_t spin_lock_blocking(spin_lock_t *l)` | 關中斷 + 上鎖，回傳舊中斷狀態 |
| `void spin_unlock(spin_lock_t *l, uint32_t saved)` | 解鎖 + 還原中斷 |
| `spin_lock_unsafe_blocking(l)` / `spin_unlock_unsafe(l)` | 不動中斷（中斷已關時用） |
| `bool is_spin_locked(l)`、`spin_lock_unclaim(n)`、`next_striped_spin_lock_num()` | |
| `critical_section_init(&cs)` / `critical_section_init_with_lock_num(&cs, n)` / `critical_section_deinit(&cs)` | |
| `critical_section_enter_blocking(&cs)` / `critical_section_exit(&cs)` | |
| `mutex_init(&m)`、`auto_init_mutex(name);`、`mutex_enter_blocking(&m)`、`mutex_try_enter(&m, &owner)`、`mutex_enter_timeout_ms(&m, ms)`、`mutex_exit(&m)` | |
| `recursive_mutex_init`、`recursive_mutex_enter_blocking`、`recursive_mutex_exit` | |
| `uint32_t save_and_disable_interrupts(void)` / `void restore_interrupts(uint32_t)` | 只影響目前核心 |
| `multicore_launch_core1(void (*entry)(void))`、`multicore_reset_core1()` | |
| `multicore_fifo_push_blocking(u32)`、`multicore_fifo_pop_blocking()`、`multicore_fifo_rvalid()`、`multicore_fifo_wready()`、`multicore_fifo_drain()`、`multicore_fifo_push_timeout_us(u32, us)`、`multicore_fifo_pop_timeout_us(us, &out)` | |
| `uint get_core_num(void)` | 0 或 1 |

RP2350 小知識：硬體 spin lock 有勘誤 E2，SDK 在 RP2350 **預設改用軟體 spin lock**（`PICO_USE_SW_SPIN_LOCKS=1`），API 名稱完全不變。

### 4.3 pico-sdk 範本（已語法檢查）

```c
#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/sync.h"            // critical_section / mutex / semaphore
#include "hardware/sync.h"        // spin lock、save_and_disable_interrupts

static volatile uint32_t counter;

// ---------- 1. 硬體 spin lock（跨核心 + 關中斷）----------
static spin_lock_t *lk;
void init_spin(void) {
    int n = spin_lock_claim_unused(true);     // 取得一個 lock 編號
    lk = spin_lock_init((uint)n);              // 拿到 spin_lock_t*
}
void inc_spin(void) {
    uint32_t save = spin_lock_blocking(lk);    // 關中斷 + 上鎖，回傳舊中斷狀態
    counter++;                                 // 臨界區越短越好
    spin_unlock(lk, save);                     // 解鎖 + 還原中斷
}

// ---------- 2. critical_section（包好的 spin lock，最常用）----------
static critical_section_t cs;
void inc_cs(void) {
    critical_section_enter_blocking(&cs);
    counter++;
    critical_section_exit(&cs);
}

// ---------- 3. mutex（可睡、可被中斷；不能在 IRQ 裡用）----------
auto_init_mutex(my_mtx);                       // 靜態宣告 + 自動初始化
static mutex_t mtx2;
void inc_mutex(void) {
    mutex_enter_blocking(&my_mtx);
    counter++;
    mutex_exit(&my_mtx);
}

// ---------- 4. 只擋本核心的中斷（單核 + ISR 共用變數）----------
void inc_irqsafe(void) {
    uint32_t s = save_and_disable_interrupts();
    counter++;
    restore_interrupts(s);
}

// ---------- 5. 雙核心 + FIFO ----------
#define ROUNDS 100000
static void core1_entry(void) {
    for (int i = 0; i < ROUNDS; i++) inc_cs();
    multicore_fifo_push_blocking(1);           // 告訴 core0 做完了
    while (1) tight_loop_contents();
}

int main(void) {
    stdio_init_all();
    init_spin();
    critical_section_init(&cs);
    mutex_init(&mtx2);
    counter = 0;
    multicore_launch_core1(core1_entry);
    for (int i = 0; i < ROUNDS; i++) inc_cs();
    uint32_t done = multicore_fifo_pop_blocking();
    printf("done=%lu counter=%lu (expect %d)\n", (unsigned long)done, (unsigned long)counter, 2 * ROUNDS);
    inc_spin(); inc_mutex(); inc_irqsafe();
    printf("core=%u\n", get_core_num());
    critical_section_deinit(&cs);
    return 0;
}
```

### 4.4 pthread / C11 atomic 範本（Linux 上編譯執行過，輸出 400000×3）

```c
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>

#define W 4
#define R 100000
static long c_spin, c_mtx;
static atomic_long c_atom;
static pthread_spinlock_t sl;
static pthread_mutex_t mx = PTHREAD_MUTEX_INITIALIZER;

static void *worker(void *arg) {
    (void)arg;
    for (int i = 0; i < R; i++) {
        pthread_spin_lock(&sl);   c_spin++;  pthread_spin_unlock(&sl);
        pthread_mutex_lock(&mx);  c_mtx++;   pthread_mutex_unlock(&mx);
        atomic_fetch_add_explicit(&c_atom, 1, memory_order_relaxed);   // 無鎖計數
    }
    return NULL;
}

int main(void) {
    pthread_spin_init(&sl, PTHREAD_PROCESS_PRIVATE);
    pthread_t th[W];
    for (int i = 0; i < W; i++) pthread_create(&th[i], NULL, worker, NULL);
    for (int i = 0; i < W; i++) pthread_join(th[i], NULL);
    printf("%ld %ld %ld (expect %d)\n", c_spin, c_mtx, (long)atomic_load(&c_atom), W * R);
    pthread_spin_destroy(&sl);
    pthread_mutex_destroy(&mx);
    return 0;
}
```
編譯：`gcc -O2 -Wall -pthread pth.c -o pth`

| pthread | 說明 |
|---|---|
| `pthread_spin_init(&l, PTHREAD_PROCESS_PRIVATE)` / `pthread_spin_lock` / `pthread_spin_trylock` / `pthread_spin_unlock` / `pthread_spin_destroy` | 忙等，適合極短臨界區 |
| `PTHREAD_MUTEX_INITIALIZER` / `pthread_mutex_lock` / `pthread_mutex_trylock` / `pthread_mutex_unlock` | 會睡，適合較長 |
| `pthread_create(&t, NULL, fn, arg)` / `pthread_join(t, NULL)` | |
| `atomic_fetch_add(&x, 1)`、`atomic_load`、`atomic_store`、`atomic_compare_exchange_strong` | `<stdatomic.h>` |

---

## 5. 效能優化

> 這節是精簡版。完整的優化手冊（前後對照範例、實測數字、什麼時候用）在 [optimization.md](optimization.md)。

### 5.1 清單（由大到小）

| 招式 | 怎麼做 | 為什麼 |
|---|---|---|
| 讓硬體搬資料 | DMA 取代 `for` 複製；PIO 取代 bit-bang | CPU 空出來做別的（練習題重點） |
| 熱點放 SRAM | `__not_in_flash_func(name)` 或 `__time_critical_func(name)`；整支：`pico_set_binary_type(app copy_to_ram)` | flash XIP cache miss 會卡 |
| 編譯最佳化 | SDK 預設 `CMAKE_BUILD_TYPE=Release`（GCC 實際是 `-g -O3 -DNDEBUG`，已經是 -O3）；確認不是 Debug（`-Og`）；可試 `target_compile_options(app PRIVATE -O2)` 比較；單函式 `__attribute__((optimize("O3")))`（GCC 才吃，clang 會忽略） | |
| 雙核心 | `multicore_launch_core1`，資料切半 | |
| 減少鎖 | 鎖外運算、每核心各自累加最後再合併 | |
| 位元技巧 | 用移位/遮罩取代除法與取餘（2 的冪）；`__builtin_popcount/clz/ctz` 在 RP2350 RISC-V 直接變成單一指令 | |
| 插值器 | `hardware/interp.h`：查表位址、混合運算 | 每核心一組 `interp0`/`interp1` |
| 提高時脈 | `set_sys_clock_khz(khz, true)` | 先確認考題允許；先 `vreg_set_voltage`；之後 UART 重新 init、PIO 分頻重算（見 optimization.md 第 6 節） |
| 不要 printf 在熱迴圈 | USB/UART 很慢 | |

### 5.2 計時範本（已語法檢查）

```c
#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/clocks.h"

// ---------- 計時 ----------
static inline uint64_t now_us(void) { return time_us_64(); }

// RISC-V（Hazard3）週期計數器：mcycle 開機時被 mcountinhibit 擋住，要先清掉 bit0
#if defined(__riscv)
static inline void cycle_counter_enable(void) {
    __asm__ volatile ("csrci 0x320, 1");         // mcountinhibit.cy = 0
}
static inline uint32_t cycles(void) {
    uint32_t c;
    __asm__ volatile ("csrr %0, mcycle" : "=r"(c));
    return c;
}
#endif

// ---------- 放進 SRAM 執行，避開 flash XIP cache miss ----------
uint32_t __not_in_flash_func(hot_loop)(const uint32_t *a, uint n) {
    uint32_t s = 0;
    for (uint i = 0; i < n; i++) s += a[i];
    return s;
}
// 另一個寫法：__time_critical_func(name) 也是放 RAM（名稱上強調時間關鍵）
void __time_critical_func(isr_like)(void) { }

// 單一函式強制 O3 / 展開（GCC 屬性）
__attribute__((optimize("O3"))) uint32_t sum_o3(const uint32_t *a, uint n) {
    uint32_t s = 0;
    for (uint i = 0; i < n; i++) s += a[i];
    return s;
}

static uint32_t buf[4096];

int main(void) {
    stdio_init_all();
    printf("sys clk = %lu Hz\n", (unsigned long)clock_get_hz(clk_sys));
    absolute_time_t t0 = get_absolute_time();
    uint32_t r = hot_loop(buf, 4096);
    int64_t dt = absolute_time_diff_us(t0, get_absolute_time());
    printf("r=%lu %lld us\n", (unsigned long)r, (long long)dt);

    uint64_t a = now_us();
    r = sum_o3(buf, 4096);
    printf("%llu us\n", (unsigned long long)(now_us() - a));
#if defined(__riscv)
    cycle_counter_enable();
    uint32_t c0 = cycles();
    r = hot_loop(buf, 4096);
    printf("%lu cycles\n", (unsigned long)(cycles() - c0));
#endif
    isr_like();
    return (int)r;
}
```

| 計時 API | 說明 |
|---|---|
| `uint64_t time_us_64(void)` / `uint32_t time_us_32(void)` | 開機後微秒 |
| `absolute_time_t get_absolute_time(void)`、`int64_t absolute_time_diff_us(from, to)`、`to_us_since_boot(t)` | |
| `sleep_ms` / `sleep_us` / `busy_wait_us_32` / `busy_wait_us` | |
| `clock_get_hz(clk_sys)` | 目前系統時脈 |
| RISC-V：`csrr rd, mcycle`（`mcycleh` 高 32 位）、`minstret` | **開機時被 `mcountinhibit` 擋住**，先 `csrci 0x320, 5`（bit0 開 `mcycle`、bit2 開 `minstret`） |

### 5.3 插值器範本（已語法檢查）

```c
#include "pico/stdlib.h"
#include "hardware/interp.h"
#if defined(__riscv)
#include "hardware/hazard3.h"   // 只有 RISC-V 有這個標頭，Arm 編譯會找不到
#endif

// 查表：result = table + ((idx >> 0) & 0xFF) * 4，一次 pop 出位址
uint32_t lut_sum(const uint32_t *table, const uint8_t *idx, uint n) {
    interp_config cfg = interp_default_config();
    interp_config_set_shift(&cfg, 0);
    interp_config_set_mask(&cfg, 2, 9);          // 保留 bit2..9 = 8-bit index * 4
    interp_set_config(interp0, 0, &cfg);
    interp0->base[0] = (uint32_t)(uintptr_t)table;
    uint32_t s = 0;
    for (uint i = 0; i < n; i++) {
        interp0->accum[0] = (uint32_t)idx[i] << 2;
        s += *(const uint32_t *)(uintptr_t)interp0->peek[0];   // peek：不改 accum
    }
    return s;
}
#if defined(__riscv)
uint32_t field(uint32_t x) { return __hazard3_bextmi(4, x, 8); }  // (x >> 8) & 0xF，RP2350 自訂指令
#endif
int main(void) { static uint32_t t[256]; uint8_t i[4] = {1,2,3,4}; return (int)lut_sum(t, i, 4); }
```
插值器 API：`interp_default_config`、`interp_config_set_shift`、`interp_config_set_mask(&c, lsb, msb)`、`interp_config_set_signed`、`interp_config_set_cross_input`、`interp_config_set_add_raw`、`interp_set_config(interp0, lane, &c)`、`interp_set_base`、`interp_set_accumulator`、`interp_peek_lane_result`、`interp_pop_lane_result`、`interp_peek_full_result`、`interp_save` / `interp_restore`。暫存器：`interpN->accum[0..1]`、`base[0..2]`、`peek[0..2]`、`pop[0..2]`。

### 5.4 位元運算工具（Linux 上執行過，對照練習題 `demo_asm.c` 的數值）

```c
#include <stdint.h>
#include <stdio.h>
#define BIT(n)            (1u << (n))
#define SET(x,n)          ((x) |  BIT(n))
#define CLR(x,n)          ((x) & ~BIT(n))
#define TOG(x,n)          ((x) ^  BIT(n))
#define GET(x,n)          (((x) >> (n)) & 1u)
#define MASK(w)           ((w) >= 32 ? 0xFFFFFFFFu : (BIT(w) - 1u))
#define FIELD_GET(x,lo,w) (((x) >> (lo)) & MASK(w))
#define FIELD_SET(x,lo,w,v) (((x) & ~(MASK(w) << (lo))) | (((v) & MASK(w)) << (lo)))
static inline int is_pow2(uint32_t x) { return x && !(x & (x - 1)); }
static inline uint32_t lowbit(uint32_t x) { return x & -x; }
static inline uint32_t clear_lowbit(uint32_t x) { return x & (x - 1); }
static inline uint32_t align_up(uint32_t x, uint32_t a) { return (x + a - 1) & ~(a - 1); }  /* a 為 2 的冪 */
static inline uint32_t rotl32(uint32_t x, unsigned r) { r &= 31; return (x << r) | (x >> ((32 - r) & 31)); }
static inline int32_t sext(uint32_t x, unsigned bits) { uint32_t m = 1u << (bits - 1); return (int32_t)((x ^ m) - m); }
static inline uint32_t popcnt(uint32_t x) { x = x - ((x >> 1) & 0x55555555u); x = (x & 0x33333333u) + ((x >> 2) & 0x33333333u); return (((x + (x >> 4)) & 0x0F0F0F0Fu) * 0x01010101u) >> 24; }
static inline uint32_t bitrev32(uint32_t x) {
    x = ((x >> 1) & 0x55555555u) | ((x & 0x55555555u) << 1);
    x = ((x >> 2) & 0x33333333u) | ((x & 0x33333333u) << 2);
    x = ((x >> 4) & 0x0F0F0F0Fu) | ((x & 0x0F0F0F0Fu) << 4);
    x = ((x >> 8) & 0x00FF00FFu) | ((x & 0x00FF00FFu) << 8);
    return (x >> 16) | (x << 16);
}
int main(void) {
    uint32_t x = 0xA5;
    printf("%X %X %X %u %X %X\n", SET(x,1), CLR(x,0), TOG(x,7), GET(x,7), FIELD_GET(x,4,4), FIELD_SET(x,4,4,0x3));
    printf("%d %X %X %u %X\n", is_pow2(64), lowbit(0x58), clear_lowbit(0x58), align_up(13,8), rotl32(0x80000001u,1));
    printf("%d %u %u %X\n", sext(0xFF,8), popcnt(0xF0F0F0F0u), (unsigned)__builtin_popcount(0xF0F0F0F0u), bitrev32(1));
    return 0;
}
```
輸出：`A7 A4 25 1 A 35` / `1 8 50 16 3` / `-1 16 16 80000000`

---

## 6. RISC-V（RV32，RP2350 的 Hazard3 核心）

### 6.1 RP2350 支援哪些擴充

SDK 給 RP2350 RISC-V 的編譯旗標（`cmake/preload/toolchains/pico_riscv_gcc.cmake`）：
`-march=rv32imac_zicsr_zifencei_zba_zbb_zbs_zbkb -mabi=ilp32`（新版工具鏈另加 `zca_zcb_zcmp`）。
**沒有 F/D（沒有硬體浮點）、沒有 Zbc（無 clmul）、沒有 V。** 另有 Hazard3 自訂 `Xh3bextm`（SDK 巨集 `__hazard3_bextm` / `__hazard3_bextmi`，在 `hardware/hazard3.h`）。

### 6.2 暫存器與呼叫慣例（ilp32）

| 暫存器 | ABI 名 | 用途 | 誰保存 |
|---|---|---|---|
| x0 | `zero` | 永遠 0 | — |
| x1 | `ra` | 回傳位址 | caller |
| x2 | `sp` | 堆疊指標（16-byte 對齊） | callee |
| x3 / x4 | `gp` / `tp` | 全域 / 執行緒指標 | — |
| x5–x7, x28–x31 | `t0`–`t6` | 暫存 | caller |
| x8 | `s0`/`fp` | 保存 / frame pointer | callee |
| x9, x18–x27 | `s1`–`s11` | 保存 | callee |
| x10–x11 | `a0`–`a1` | 參數 + 回傳值 | caller |
| x12–x17 | `a2`–`a7` | 參數 | caller |

### 6.3 常用指令

| 類 | 指令 |
|---|---|
| 算術 | `add sub addi lui auipc`、`slt sltu slti sltiu` |
| 邏輯/移位 | `and or xor andi ori xori`、`sll srl sra slli srli srai` |
| 記憶體 | `lw lh lhu lb lbu`、`sw sh sb`（`lw rd, off(rs1)`） |
| 分支 | `beq bne blt bge bltu bgeu`、`jal jalr` |
| 虛擬指令 | `li mv nop not neg j jr ret call beqz bnez` |
| M | `mul mulh mulhu mulhsu div divu rem remu` |
| A | `lr.w sc.w amoswap.w amoadd.w amoand.w amoor.w amoxor.w amomax.w amomin.w` |
| Zicsr | `csrr csrw csrs csrc csrrw csrrs csrrc`、立即數版 `csrwi csrsi csrci` |
| Zba | `sh1add sh2add sh3add`（`a + (b << n)`，陣列索引） |
| Zbb | `andn orn xnor clz ctz cpop max maxu min minu sext.b sext.h zext.h rol ror rori orc.b rev8` |
| Zbs | `bset bclr binv bext` 與立即數版 `bseti bclri binvi bexti` |
| Zbkb | `pack packh brev8 zip unzip`（以及與 Zbb 重疊的 `rol ror andn orn xnor rev8`） |

clang 以 RP2350 `-march -O2` 實測：`__builtin_clz`→`clz`、`__builtin_ctz`→`ctz`、`__builtin_popcount`→`cpop`、`__builtin_bswap32`→`rev8`、`a & ~b`→`andn`、循環左移→`rol`、`a[i]`→`sh2add`、`x | (1u<<i)`→`bset`、`(x>>i)&1`→`bext`、`x & 0xFFFF`→`zext.h`。**用 builtin 就好，不必手寫 asm。**

### 6.4 GCC inline asm

```c
uint32_t r;
__asm__ volatile ("add %0, %1, %2" : "=r"(r) : "r"(a), "r"(b));   // 輸出 : 輸入
__asm__ volatile ("addi %0, %0, 1" : "+r"(x));                    // "+r" 讀寫同一個
__asm__ volatile ("slli %0, %1, %2" : "=r"(r) : "r"(a), "i"(3));  // "i" 立即數
__asm__ volatile ("" ::: "memory");                               // compiler barrier
__asm__ volatile ("fence" ::: "memory");                          // 硬體記憶體屏障
```
約束：`r` 暫存器、`i` 立即數、`m` 記憶體、`=` 只寫、`+` 讀寫、`&` early-clobber；第三段 clobber 填 `"memory"` 或被改的暫存器名。

### 6.5 獨立 `.S` 函式（已組譯過）

```asm
# uint32_t asm_sum(const uint32_t *a, uint32_t n);   a0 = a, a1 = n, 回傳 a0
    .section .text.asm_sum, "ax"   # 要放 RAM：改成 .time_critical.asm_sum, "ax"
    .globl asm_sum
    .type asm_sum, @function
asm_sum:
    li    t0, 0                 # t0 = sum
    beqz  a1, 2f
1:  lw    t1, 0(a0)             # t1 = *a
    add   t0, t0, t1
    addi  a0, a0, 4             # a++（位元組位移 4）
    addi  a1, a1, -1
    bnez  a1, 1b
2:  mv    a0, t0
    ret
    .size asm_sum, .-asm_sum
```
C 端宣告：`uint32_t asm_sum(const uint32_t *a, uint32_t n);`，CMake `add_executable` 直接加 `sum.S`，project 要含 `ASM`。

---

## 7. ROS 2 Jazzy

### 7.1 rclpy 範本（對照 jazzy 分支 rclpy 原始碼；Python 語法檢查過）

```python
"""一支同時示範 publisher、subscriber、timer、QoS、參數的 rclpy node。"""
import rclpy
from rclpy.node import Node
from rclpy.qos import QoSProfile, ReliabilityPolicy, HistoryPolicy, DurabilityPolicy
from rclpy.qos import qos_profile_sensor_data
from std_msgs.msg import Int32, String


class Worker(Node):
    def __init__(self):
        super().__init__('worker')                       # node 名稱 -> /worker
        self.declare_parameter('gain', 2)                # ros2 param set /worker gain 3
        qos = QoSProfile(depth=10,
                         reliability=ReliabilityPolicy.RELIABLE,
                         history=HistoryPolicy.KEEP_LAST,
                         durability=DurabilityPolicy.VOLATILE)
        self.sub = self.create_subscription(Int32, '/number', self.on_msg, qos)
        self.pub = self.create_publisher(Int32, '/double', qos)
        self.log_pub = self.create_publisher(String, '/status', 10)   # 10 = depth 的簡寫
        self.sensor_sub = self.create_subscription(Int32, '/sensor', self.on_msg, qos_profile_sensor_data)
        self.timer = self.create_timer(0.5, self.tick)   # 秒；0.5 = 2 Hz
        self.count = 0

    def on_msg(self, msg):
        gain = self.get_parameter('gain').value
        out = Int32()
        out.data = msg.data * gain
        self.pub.publish(out)
        self.count += 1

    def tick(self):
        s = String()
        s.data = f'handled {self.count}'
        self.log_pub.publish(s)
        self.get_logger().info(s.data)


def main():
    rclpy.init()
    node = Worker()
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

| rclpy | 簽名重點 |
|---|---|
| `rclpy.init()` / `rclpy.spin(node)` / `rclpy.spin_once(node, timeout_sec=0.1)` / `rclpy.try_shutdown()` / `rclpy.shutdown()` / `rclpy.ok()` | |
| `Node.create_publisher(msg_type, topic, qos_profile)` | qos 可給 `int` = depth |
| `Node.create_subscription(msg_type, topic, callback, qos_profile)` | callback 在 qos **前面** |
| `Node.create_timer(timer_period_sec, callback)` | 秒，float |
| `Node.create_service(srv_type, name, callback)` / `create_client(srv_type, name)` | |
| `Node.declare_parameter(name, value)` / `get_parameter(name).value` | |
| `Node.get_logger().info(...)` / `.warning(...)` / `.error(...)` | |
| `QoSProfile(depth=10, reliability=ReliabilityPolicy.RELIABLE / BEST_EFFORT, history=HistoryPolicy.KEEP_LAST / KEEP_ALL, durability=DurabilityPolicy.VOLATILE / TRANSIENT_LOCAL)` | `from rclpy.qos import ...` |
| 預設組：`qos_profile_sensor_data`（BEST_EFFORT）、`qos_profile_system_default`、`qos_profile_services_default` | |
| `from rclpy.executors import MultiThreadedExecutor, SingleThreadedExecutor`；`ex.add_node(n)`；`ex.spin()` | |
| `from rclpy.callback_groups import MutuallyExclusiveCallbackGroup, ReentrantCallbackGroup` | |

**QoS 不相容就收不到**：publisher 是 BEST_EFFORT、subscriber 要 RELIABLE → 不連線、不報錯。用 `ros2 topic info -v /topic` 看兩邊 QoS。

### 7.2 rclcpp 範本（名稱對照 jazzy 分支 rclcpp 標頭；**未實際編譯**）

```cpp
#include <chrono>
#include <memory>
#include "rclcpp/rclcpp.hpp"
#include "std_msgs/msg/int32.hpp"
using namespace std::chrono_literals;

class Doubler : public rclcpp::Node {
public:
  Doubler() : Node("doubler") {
    pub_ = create_publisher<std_msgs::msg::Int32>("/double", 10);
    sub_ = create_subscription<std_msgs::msg::Int32>(
      "/number", rclcpp::QoS(10).reliable(),
      [this](const std_msgs::msg::Int32 & msg) {
        std_msgs::msg::Int32 out;
        out.data = msg.data * 2;
        pub_->publish(out);
      });
    timer_ = create_wall_timer(500ms, [this]() { RCLCPP_INFO(get_logger(), "alive"); });
  }

private:
  rclcpp::Publisher<std_msgs::msg::Int32>::SharedPtr pub_;
  rclcpp::Subscription<std_msgs::msg::Int32>::SharedPtr sub_;
  rclcpp::TimerBase::SharedPtr timer_;
};

int main(int argc, char ** argv) {
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<Doubler>());
  rclcpp::shutdown();
  return 0;
}
```
QoS：`rclcpp::QoS(10).reliable()` / `.best_effort()` / `.transient_local()` / `.durability_volatile()` / `.keep_last(n)` / `.keep_all()`；`rclcpp::SensorDataQoS()`、`rclcpp::SystemDefaultsQoS()`。

### 7.3 ros2 CLI（對照 jazzy 分支 ros2cli）

| 指令 | 用途 |
|---|---|
| `ros2 node list` / `ros2 node info /node` | node 與其 pub/sub |
| `ros2 topic list -t` | 列 topic 含型別 |
| `ros2 topic info /t -v` | 型別、pub/sub 數、**各端 QoS** |
| `ros2 interface show std_msgs/msg/Int32` | 訊息欄位 |
| `ros2 topic echo /t` / `--once` / `--field data` | 看資料 |
| `ros2 topic hz /t` / `ros2 topic bw /t` | 頻率 / 頻寬 |
| `ros2 topic pub --once /t std_msgs/msg/Int32 "{data: 21}"` | 發一筆 |
| `ros2 topic pub -r 10 /t std_msgs/msg/Int32 "{data: 1}"` / `-t 5`（發 5 次） | 固定頻率 |
| `ros2 topic pub ... --qos-reliability best_effort` | echo/pub/hz 都有 `--qos-*` 參數 |
| `ros2 service list` / `ros2 service type /s` / `ros2 service call /s pkg/srv/T "{...}"` | |
| `ros2 param list` / `ros2 param get /n p` / `ros2 param set /n p v` / `ros2 param dump /n` | |
| `ros2 run <pkg> <exe>` / `ros2 pkg create --build-type ament_python --node-name n my_pkg` | |
| `ros2 doctor` / `ros2 doctor hello` | 環境 / 網路檢查 |
| 環境變數 `ROS_DOMAIN_ID` | 不同 ID 互相看不到 |

除錯順序（練習題 2-1）：node 在嗎 → topic 在嗎 → 兩端名稱一樣嗎（差一個字就是另一個 topic）→ pub/sub 數合理嗎 → QoS 相容嗎 → 真的有資料嗎（echo / hz）→ 用 `topic pub` 餵已知值再 echo 輸出。

---

## 8. 建置

### 8.1 pico-sdk CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.13)
include(pico_sdk_import.cmake)          # 從 pico-sdk/external/ 複製過來
project(app C CXX ASM)
set(CMAKE_C_STANDARD 11)
pico_sdk_init()

add_executable(app main.c)
pico_generate_pio_header(app ${CMAKE_CURRENT_LIST_DIR}/echo.pio)   # 產生 echo.pio.h
target_link_libraries(app PRIVATE
    pico_stdlib hardware_dma hardware_pio hardware_irq
    pico_multicore pico_sync hardware_interp)
target_compile_options(app PRIVATE -O3)        # 預設 build type 已是 Release
# pico_set_binary_type(app copy_to_ram)        # 整支程式開機時複製到 SRAM 執行
pico_enable_stdio_usb(app 1)
pico_enable_stdio_uart(app 0)
pico_add_extra_outputs(app)                    # 產生 .uf2 / .hex / .bin
```

```bash
mkdir build && cd build
cmake -DPICO_BOARD=pico2 -DPICO_PLATFORM=rp2350-riscv ..   # RISC-V 核心
# cmake -DPICO_BOARD=pico2 -DPICO_PLATFORM=rp2350-arm-s .. # Arm 核心（pico2 預設）
make -j
```
平台名稱（SDK 原文）：`rp2040`、`rp2350`、`rp2350-arm-s`、`rp2350-riscv`、`host`。
程式內判斷：`#if PICO_RP2350`、`#if PICO_RISCV`、`#if defined(__riscv)`。

### 8.2 ament（ROS 2）

```bash
colcon build --symlink-install            # 在 workspace 根目錄
colcon build --packages-select my_pkg
source install/setup.bash
```
ament_cmake 的 CMakeLists 主要幾行：`find_package(ament_cmake REQUIRED)`、`find_package(rclcpp REQUIRED)`、`find_package(std_msgs REQUIRED)`、`add_executable(doubler src/doubler.cpp)`、`ament_target_dependencies(doubler rclcpp std_msgs)`、`install(TARGETS doubler DESTINATION lib/${PROJECT_NAME})`、`ament_package()`。（colcon 與 ament_cmake 未對照原始碼，屬常用寫法）

---

## 9. 讓考場的小模型不亂編：prompt 範本

把 `<<...>>` 換掉，API 區塊從本筆記複製（越少越好，只貼這題用得到的那張表 + 一個範本）。

```text
你是嵌入式 C 程式助手。規則：
1. 只能使用「允許的 API」清單裡出現的函式、巨集、型別、欄位名稱。清單外的名稱一律不准用，包括你記得的名稱。
2. 如果清單不夠完成需求，不要猜，直接回答「缺少 API：<你需要的功能>」。
3. 參數順序以清單中的簽名為準，不要自行調換。
4. 輸出完整可編譯的程式碼，不要省略 include。
5. 寫完後列出你用到的每個 API 名稱，逐一標明它在清單的哪一行。

【允許的 API】
<<貼上本筆記對應的 API 表，例如第 2.1 節>>

【參考範本】
<<貼上本筆記對應的範本，例如第 2.3 節的 dma_copy_words>>

【考題提供的介面／文件摘錄】
<<貼上考題 header 或文件中的函式宣告>>

【需求】
<<一句話描述要做什麼，含資料大小、單位（筆數還是 bytes）、輸入輸出位址>>
```

再補一句常用的追問：
```text
檢查你剛才的程式：每個函式名稱是否都在【允許的 API】中？count 參數的單位是「筆數」還是「bytes」？請只回報問題，不要重寫。
```

小模型適合做：照範本改參數、補樣板、翻譯註解、解釋錯誤訊息。不適合做：從零寫 PIO 程式、選 API、算時序。

---

## 10. 常見坑總表

| 坑 | 正解 |
|---|---|
| DMA count 填 bytes | count = 筆數 = bytes / 每筆大小 |
| `dma_channel_configure` 位址順序 | **寫入端在前**、讀取端在後 |
| 預設 `write_increment=false` | 記憶體複製要設 `true` |
| 寫 FIFO/周邊卻遞增位址 | 周邊那端 increment 要 `false` |
| DMA IRQ 沒清 | handler 裡 `dma_channel_acknowledge_irq0(ch)` 或 `dma_hw->ints0 = 1u << ch` |
| PIO→記憶體只等 TX 通道 | 要等 **RX 通道** 做完才代表全部收完 |
| DMA 讀的來源是區域變數 | 傳輸期間變數失效 → 用 `static` 或全域 |
| ring buffer 沒對齊 | `__attribute__((aligned(1 << size_bits)))` |
| `pio_sm_init` 後沒動 | 還要 `pio_sm_set_enabled(pio, sm, true)` |
| 腳位沒反應 | `pio_gpio_init` + `pio_sm_set_consecutive_pindirs(..., true)` + 對的 `sm_config_set_*_pins` |
| autopull 門檻/方向 | `sm_config_set_out_shift(&c, shift_right, autopull, threshold)`；shift left 先送 bit31 |
| `.side_set` 後 delay 變短 | delay 與 side-set 共用 5 bit |
| 程式超過 32 條 | 一個 PIO 區塊只有 32 條，所有 SM 共用 |
| RP2350 新指令組譯失敗 | 檔頭加 `.pio_version 1`，並設對 `.fifo` |
| 鎖住整段運算 | 只鎖讀-改-寫那幾行 |
| 在 IRQ 裡用 mutex | IRQ 只能用 spin lock / critical section |
| `mcycle` 一直是 0 | 先 `csrci 0x320, 1`（清 `mcountinhibit.cy`） |
| RISC-V 上用 float 很慢 | RP2350 Hazard3 沒有 FPU；能用整數/定點就用 |
| ROS topic 有、資料沒 | 名稱拼錯、QoS 不相容、node 沒 spin |
| 用了新版 SDK 才有的名稱 | `dma_channel_config_t`、`dma_encode_transfer_count` 是 2.2.0 才有；用 `dma_channel_config` + 直接傳數字最保險 |

---

### 未查證 / 限制

- RP2350 datasheet PDF 這次抓不到，PIO 指令細節以 SDK 的 pioasm 原始碼與 RP2040 時代的既有行為為準；`mov` 到 ISR/OSR 會不會重設移位計數等細節（`pull noblock` 空時載入 X 已由 pico-examples 確認），考前若有時間對一下 datasheet PIO 章節。
- rclcpp 範本與 colcon / ament_cmake 寫法只對照名稱，沒有實際編譯。
- 硬體行為（時序、實際效能數字）都沒有在開發板上跑過。
