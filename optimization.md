# RP2350 / Linux 效能優化速查（上機考用）

> 這是 [cheatsheet.md](cheatsheet.md) 的續篇，專講「讓程式更快」。
> **查證方式**：pico-sdk 名稱對照 `raspberrypi/pico-sdk` 2.3.1 原始碼，C 範例用 SDK 標頭以 clang 對 RISC-V（`rp2350-riscv`）與 Arm（Cortex-M33）兩種目標做過語法檢查；`.pio` 用 SDK 的 `pioasm` 組譯過；演算法技巧在 Linux 上以 200 萬筆隨機輸入比對「前後結果完全一致」；組合語言輸出是 clang 以 RP2350 的 `-march` 實際產生的；pthread 範例在 4 核 Linux VM 上跑過。
> **範本裡自己定義的函式不是 SDK API**（例如 `dma_copy_start`、`dma_sum`、`dma_handler`、`ring_capture_start`、`pingpong_start`、`consume`、`spsc_push`/`spsc_pop`、`sum_parallel`、`split_init`、`overclock`、`bench`、`measure`、`cyc`、`pin_table`、`bus_dma_first`、`par_after_init`），只能整段照抄；表格裡列的才是 SDK 名稱。
> RP2350 datasheet 本身這次下載被擋，**只出現在 datasheet、沒能對到 SDK 原始碼的數字一律標「未查證」**。Hazard3 指令週期數取自 Hazard3 官方 repo 文件（RP2350 用的就是 Hazard3），不是 RP2350 datasheet。

## 目錄
1. 方法：先量再改、回報加速比、優化順序清單
2. 記憶體與放置（XIP / SRAM / scratch / 匯流排）
3. 編譯器（最佳化等級、提示、看組合語言）
4. 演算法微技巧（查表、無分支、避免除法、定點數）
5. 卸載與平行（DMA 乒乓、環形、sniff、PIO、雙核心、無鎖佇列）
6. 時脈與電壓
7. Linux pthread
9. 叫考場小模型優化用的 prompt
10. 未查證清單

---

## 1. 方法：先量再改

**原則**：每次只改一件事 → 量 → 結果要跟原版一樣 → 才留下。沒量過的「優化」不算。

### 1.1 量測框架（已語法檢查，兩種核心都能用）

```c
// 量測框架：同一段程式跑 R 次取最小值，回報 us 與 speedup
#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/timer.h"

typedef void (*fn_t)(void);
static uint64_t best_us(fn_t f, int reps) {
    uint64_t best = UINT64_MAX;
    for (int r = 0; r < reps; r++) {
        uint64_t t0 = time_us_64();
        f();
        uint64_t dt = time_us_64() - t0;
        if (dt < best) best = dt;
    }
    return best;
}
extern void before(void), after(void);
void bench(void) {
    uint64_t a = best_us(before, 20), b = best_us(after, 20);
    printf("before %llu us, after %llu us, speedup %.2fx\n",
           (unsigned long long)a, (unsigned long long)b, (double)a / (double)b);
}
```

```c
// 兩種核心通用的週期計數器
#include "pico/stdlib.h"
#if defined(__riscv)
static inline void cyc_init(void) { __asm__ volatile ("csrci mcountinhibit, 5"); } // 清 CY(bit0) 與 IR(bit2)
static inline uint32_t cyc(void)  { uint32_t c; __asm__ volatile ("csrr %0, mcycle"   : "=r"(c)); return c; }
static inline uint32_t inst(void) { uint32_t c; __asm__ volatile ("csrr %0, minstret" : "=r"(c)); return c; }
#else
#include "hardware/structs/m33.h"
static inline void cyc_init(void) {
    m33_hw->demcr |= M33_DEMCR_TRCENA_BITS;            // 開 DWT
    m33_hw->dwt_cyccnt = 0;
    m33_hw->dwt_ctrl |= M33_DWT_CTRL_CYCCNTENA_BITS;    // 開 CYCCNT
}
static inline uint32_t cyc(void) { return m33_hw->dwt_cyccnt; }
#endif
uint32_t measure(void (*f)(void)) {
    cyc_init();
    uint32_t c0 = cyc(); f(); return cyc() - c0;   // 32 位元，150MHz 約 28 秒會繞回
}
```

| 量測工具 | 用法 | 注意 |
|---|---|---|
| `time_us_64()` | 微秒，最簡單 | 解析度 1 µs，太短的函式要跑很多次 |
| RISC-V `mcycle` / `minstret` | 週期數 / 指令數 | 開機時 `mcountinhibit` 擋住（重設值 CY=1、IR=1），要先 `csrci mcountinhibit, 5` |
| Arm `m33_hw->dwt_cyccnt` | 週期數 | 先開 `demcr` 的 TRCENA 和 `dwt_ctrl` 的 CYCCNTENA（暫存器名稱對照 SDK 標頭；實機未測） |
| `instret / cycles` | IPC | Hazard3 理想值接近 1；低很多代表在等記憶體或分支猜錯 |

**回報格式**（考卷、README 都能用）：`原版 X us → 改後 Y us，加速 X/Y 倍（取 20 次最小值，150 MHz，-O3）`。一定寫清楚時脈、編譯等級、量幾次取什麼值。

### 1.2 優化順序清單（OJ 計時題，通常收益由大到小）

| # | 先檢查 | 典型收益 | 細節 |
|---|---|---|---|
| 1 | 演算法複雜度：O(n²) → O(n log n)/O(n) | 數十倍以上 | 先想資料結構，再談微調 |
| 2 | 熱迴圈裡有 `printf` / `sleep` / 除法 / 浮點（RISC-V） | 數倍 | 搬出迴圈；RISC-V 沒有浮點硬體 |
| 3 | 編譯等級：確認是 Release（-O3），不是 Debug（-Og） | 2–5 倍 | 第 3 節 |
| 4 | 讓硬體搬：DMA 取代 `for` 複製、PIO 取代 bit-bang | CPU 空出來 | 第 5 節 |
| 5 | 熱程式碼放 SRAM、資料放對 bank | 看 cache miss 多寡 | 第 2 節 |
| 6 | 雙核心切工作 | 最多約 2 倍 | 第 5.5 節 |
| 7 | 位元技巧、查表、無分支、展開 | 10–50% | 第 4 節 |
| 8 | 超頻 | 與時脈成正比 | 第 6 節，先確認規則允許 |

---

## 2. 記憶體與放置

### 2.1 位址表（對照 SDK `hardware/regs/addressmap.h`）

| 區域 | 位址 | 大小 | 用途 |
|---|---|---|---|
| XIP（flash，經快取） | `0x10000000` `XIP_BASE` | 依 flash | 預設程式放這，cache miss 很慢 |
| XIP 快取當 SRAM | `0x13ffc000`–`0x14000000` `XIP_SRAM_BASE` | 16 KB | **和 RP2040 不同**：SDK 暫存器說明（`regs/xip.h` 的 `CTRL_EN_SECURE`）寫 RP2350「沒有 cache-as-SRAM 視窗」，要用就**快取保持開著、把個別快取行釘住**（`xip_cache_pin_range`）。考試別拿它當額外 RAM |
| XIP 不經快取 | `0x14000000` `XIP_NOCACHE_NOALLOC_BASE` | | 讀一次就丟的資料 |
| SRAM0–7（交錯） | `0x20000000` `SRAM_STRIPED_BASE` | 512 KB | 主記憶體，相鄰字分散到不同 bank |
| SRAM4 起 | `0x20040000` `SRAM4_BASE` | | |
| SRAM8 = scratch X | `0x20080000` `SRAM8_BASE` | 4 KB | `__scratch_x("名")` |
| SRAM9 = scratch Y | `0x20081000` `SRAM9_BASE` | 4 KB | `__scratch_y("名")` |
| 結尾 | `0x20082000` `SRAM_END` | 共 520 KB | |

XIP 快取：`XIP_CACHE_SIZE` = 16 KB，`XIP_CACHE_LINE_SIZE` = 8 bytes（SDK 標頭）。

### 2.2 放置範本（已語法檢查）

```c
// 放置：熱函式放 RAM、資料放 scratch bank、釘住 XIP cache
#include "pico/stdlib.h"
#include "hardware/xip_cache.h"
#include "hardware/structs/busctrl.h"

// 1) 熱迴圈放 SRAM，不從 flash(XIP) 取指
int32_t __not_in_flash_func(dot)(const int16_t *x, const int16_t *y, int n) {
    int32_t s = 0;
    for (int i = 0; i < n; i++) s += x[i] * y[i];
    return s;
}
// 2) __time_critical_func 同樣放 RAM（section .time_critical.*）
void __time_critical_func(fast_isr_body)(void) { }

// 3) 資料放 core 專屬 scratch bank（各 4KB），避免和 DMA/另一核搶 SRAM0-7
static int16_t __scratch_x("coef") coef[256];
static uint32_t __uninitialized_ram(big)[1024];   // 開機不清零，省啟動時間

// 4) 匯流排優先權：讓 DMA 讀寫優先（位元：PROC0=0, PROC1=4, DMA_R=8, DMA_W=12）
void bus_dma_first(void) {
    busctrl_hw->priority = BUSCTRL_BUS_PRIORITY_DMA_R_BITS | BUSCTRL_BUS_PRIORITY_DMA_W_BITS;
}
// 5) 匯流排效能計數器：數 SRAM0 被搶的次數
void bus_count_start(void) {
    busctrl_hw->counter[0].sel = arbiter_sram0_perf_event_access_contested;
    busctrl_hw->counter[0].value = 0;
    busctrl_hw->perfctr_en = 1;
}
uint32_t bus_count_read(void) { return busctrl_hw->counter[0].value; }

// 6) 把 flash 中一段唯讀表釘在 XIP cache（offset 從 XIP_BASE 算起）
extern const uint8_t table[4096];
void pin_table(void) {
    xip_cache_pin_range((uintptr_t)table - XIP_BASE, sizeof(table));
}
int use(void) { return coef[0] + (int)big[0]; }
```

| 巨集 / 設定 | 效果 | 什麼時候用 |
|---|---|---|
| `__not_in_flash_func(f)` | 函式放 SRAM | 熱迴圈、ISR |
| `__time_critical_func(f)` | 放 `.time_critical.*`，也在 SRAM | 同上，語意上強調時間關鍵 |
| `__no_inline_not_in_flash_func(f)` | 放 SRAM 且不 inline | 確保真的有一份在 RAM |
| `__scratch_x("g")` / `__scratch_y("g")` | 變數放 4 KB scratch bank（同一塊頂端是堆疊：Y 給 core0、X 給 core1，各 2 KB，所以資料別超過約 2 KB） | 每核心專用的熱資料、堆疊式暫存 |
| `__uninitialized_ram(name)` | 開機不清零（巨集會產生變數名） | 大 buffer，省開機時間 |
| `__in_flash("g")` | 強制留在 flash | 大常數表、省 RAM |
| `pico_set_binary_type(app copy_to_ram)` | 開機把整支程式複製到 SRAM 執行 | 程式小、全部都熱 |
| `pico_set_binary_type(app no_flash)` | 只在 RAM（用 debugger/USB 載入） | 實驗用 |
| `xip_cache_pin_range(offset, size)` | 把 flash 一段釘在快取 | 熱的唯讀表；offset 從 `XIP_BASE` 起算 |
| `xip_cache_invalidate_all()` / `xip_cache_clean_all()` | 清快取 | 改過 flash 內容後 |

`hardware/xip_cache.h` 是 **SDK 2.1.0 起**才有，CMake 連結 `hardware_xip_cache`。

### 2.3 匯流排搶用

- 兩顆核心和 DMA 同時打同一個 SRAM bank 會互等。交錯位址（striped）讓連續陣列自然分散；**每核心各用一塊不重疊的資料**、把各自的熱變數放 scratch X/Y，最不會搶。
- `busctrl_hw->priority`：`BUSCTRL_BUS_PRIORITY_PROC0_BITS`(bit0)、`PROC1`(bit4)、`DMA_R`(bit8)、`DMA_W`(bit12)，設 1 = 高優先。
- 量搶用：`busctrl_hw->counter[0..3].sel = arbiter_<bank>_perf_event_access_contested`，`perfctr_en = 1`，讀 `.value`（bank 名：`sram0`…`sram9`、`xip_main0/1`、`rom`、`fastperi`、`apb`、`sio_proc0/1`）。

### 2.4 對齊與結構

- 32 位元讀寫要 4-byte 對齊；`__aligned(n)`（SDK）或 `__attribute__((aligned(n)))`。
- DMA 環形 buffer 必須對齊 `1 << size_bits` 位元組（第 5.2 節）。
- 結構欄位大的放前面：`{u8,u32,u8}` 12 bytes → `{u32,u8,u8}` 8 bytes（已用 `_Static_assert` 確認）。

---

## 3. 編譯器

### 3.1 最佳化等級（對照 SDK cmake 與 CMake 3.28 預設）

| `CMAKE_BUILD_TYPE` | 實際旗標（GCC） | 備註 |
|---|---|---|
| 未指定 → **Release**（SDK 預設） | `-g -O3 -DNDEBUG` | SDK 加 `-g`，CMake 加 `-O3 -DNDEBUG` |
| Debug | `-Og -g`（`PICO_DEOPTIMIZED_DEBUG=1` 時 `-O0`） | **慢**，考試計時別用 |
| MinSizeRel | `-g -Os -DNDEBUG`（clang 工具鏈是 `-Oz`） | 程式太大放不進 RAM 時 |
| RelWithDebInfo | `-O2 -g -DNDEBUG` | |

- SDK 預設已加 `-ffunction-sections -fdata-sections` 與 `--gc-sections`（`PICO_NO_GC_SECTIONS` 可關）。
- 換等級：`cmake -DCMAKE_BUILD_TYPE=Release ..`；單一目標：`target_compile_options(app PRIVATE -O2)`。-O3 不一定比 -O2 快（程式變大、cache miss 變多），**兩個都量**。
- **LTO**：SDK 的 cmake 裡找不到相關支援 → 未查證，考場別賭。
- Linux 的 GCC 13：`-O2` 會把簡單複製迴圈換成 `memcpy`/`memset`（實測），但**不會**自動向量化未知長度的迴圈；`-O3` 才會（實測 `paddd`）。

### 3.2 給編譯器的提示（已語法檢查）

```c
// 編譯器提示：restrict、強制 inline、分支機率、展開、對齊
#include "pico/stdlib.h"

// 1) restrict：保證 dst 與 src 不重疊，編譯器才敢把 load 提前、不重讀
void scale_before(int32_t *dst, const int32_t *src, const int32_t *k, int n) {
    for (int i = 0; i < n; i++) dst[i] = src[i] * *k;      // 每圈都要重讀 *k（可能被 dst 改到）
}
void scale_after(int32_t *restrict dst, const int32_t *restrict src, const int32_t *restrict k, int n) {
    int32_t kk = *k;                                         // 或手動提出迴圈外
    for (int i = 0; i < n; i++) dst[i] = src[i] * kk;
}
// 2) 強制 inline（SDK 巨集）、不准 inline
static __force_inline int32_t sat_add(int32_t a, int32_t b) {
    int32_t r; if (__builtin_add_overflow(a, b, &r)) r = (a < 0) ? INT32_MIN : INT32_MAX; return r;
}
__attribute__((noinline)) void cold_path(void) { }   // newlib 也有 __noinline 巨集
// 3) 分支機率（SDK 沒有 likely 巨集，自己定義）
#define likely(x)   __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)
int32_t process(const int32_t *a, int n) {
    int32_t s = 0;
    for (int i = 0; i < n; i++) {
        if (unlikely(a[i] < 0)) { cold_path(); continue; }
        s = sat_add(s, a[i]);
    }
    return s;
}
// 4) 展開：GCC 8+ 與 clang 都認得 #pragma GCC unroll N
int32_t dot(const int16_t *x, const int16_t *y, int n) {
    int32_t s = 0;
#pragma GCC unroll 4
    for (int i = 0; i < n; i++) s += x[i] * y[i];
    return s;
}
// 5) 告訴編譯器對齊，可用整字存取
uint32_t sum_aligned(const uint8_t *p, int n) {
    const uint32_t *w = __builtin_assume_aligned(p, 4);
    uint32_t s = 0;
    for (int i = 0; i < n / 4; i++) s += w[i];
    return s;
}
// 6) 結構排列：大的放前面，避免 padding；常一起用的欄位放一起
typedef struct { uint8_t a; uint32_t b; uint8_t c; } bad_t;   // 12 bytes
typedef struct { uint32_t b; uint8_t a, c; } good_t;          // 8 bytes
_Static_assert(sizeof(bad_t) == 12 && sizeof(good_t) == 8, "layout");
```

`restrict` 實測差別（clang，RP2350 RISC-V，-O2）：沒加時迴圈內每圈多一個 `lw` 重讀 `*k`；加了之後 `lw a2, 0(a2)` 被提到迴圈外。

### 3.3 看組合語言

| 做法 | 指令 |
|---|---|
| SDK 自動產生 | `pico_add_extra_outputs(app)` → build 目錄有 `app.dis`（`objdump -h` 加 `-d`） |
| 手動反組譯 | RISC-V：`riscv32-unknown-elf-objdump -d app.elf`（工具鏈前綴也可能是 `riscv32-pico-elf-`、`riscv32-corev-elf-`、`riscv-none-elf-`）；Arm：`arm-none-eabi-objdump -d app.elf` |
| 只看一個函式 | `objdump -d app.elf --disassemble=函式名`（binutils 2.42 實測可用；舊版不認得就整份 `-d` 後搜尋 `<函式名>:`） |
| 直接看編譯輸出 | `gcc -O2 -S -o - file.c`（要帶跟專案一樣的 `-march`） |
| 檢查有沒有用到 Zb* | 在 `.dis` 搜 `cpop`、`ctz`、`clz`、`min`、`max`、`sh1add/sh2add/sh3add`、`rev8`、`brev8`、`andn`、`bset/bclr/binv/bext` |

### 3.4 builtin → 實際指令（clang -O2 實測）

| C 寫法 | RP2350 RISC-V（Zba/Zbb/Zbs/Zbkb） | Cortex-M33 |
|---|---|---|
| `__builtin_popcount(x)` | `cpop`（1 指令） | 約 12 指令的位元技巧（**沒有** popcount 指令） |
| `__builtin_ctz(x)` | `ctz` | `rbit` + `clz` |
| `__builtin_clz(x)` | `clz` | `clz` |
| `a<b?a:b`、clamp | `min` / `max`（無分支） | `cmp` + `it` 條件執行（無分支） |
| `x / 10`（常數） | `mulhu` + `srli`（編譯器自己換成乘法） | `umull` + `lsrs` |
| `x / y`（變數） | `divu`（Hazard3 文件：18–19 週期） | `udiv`（週期數 未查證） |
| 位元反轉 | `rev8` + `brev8`（Zbkb） | `rbit` |
| `a[i]` 位址計算 | `sh2add` | `ldr ... lsl #2` |
| `float a*b` | **函式呼叫**（軟體浮點） | 單一 FPU 指令 |
| `double a*b` | 函式呼叫 | 函式呼叫（SDK 可用 DCP 協處理器加速，見第 4.4 節） |

**結論**：常數除法、clamp、min/max 編譯器已經會做，不用手寫；真正要手動處理的是「迴圈內除以同一個變數」與「RISC-V 上的浮點」。

---

## 4. 演算法微技巧

### 4.1 前後對照（Linux 上 200 萬筆隨機輸入比對，0 筆不同）

```c
#include <stdint.h>
// ---- 1. 查表 vs 逐位元 ----
static inline int pop_before(uint32_t x){ int c=0; while(x){ c+=x&1; x>>=1; } return c; }
static const uint8_t POP8[256] = {
#define B2(n) n, n+1, n+1, n+2
#define B4(n) B2(n), B2(n+1), B2(n+1), B2(n+2)
#define B6(n) B4(n), B4(n+1), B4(n+1), B4(n+2)
    B6(0), B6(1), B6(1), B6(2)
#undef B2
#undef B4
#undef B6
};
static inline int pop_lut(uint32_t x){ return POP8[x&255]+POP8[(x>>8)&255]+POP8[(x>>16)&255]+POP8[x>>24]; }
static inline int pop_after(uint32_t x){ return __builtin_popcount(x); }   // Zbb: cpop
// ---- 2. 無分支 ----
static inline int32_t clamp_before(int32_t x,int32_t lo,int32_t hi){ if(x<lo) return lo; if(x>hi) return hi; return x; }
static inline int32_t min_bl(int32_t a,int32_t b){ return b ^ ((a ^ b) & -(a < b)); }
static inline int32_t max_bl(int32_t a,int32_t b){ return a ^ ((a ^ b) & -(a < b)); }
static inline int32_t clamp_after(int32_t x,int32_t lo,int32_t hi){ return min_bl(max_bl(x,lo),hi); }
static inline uint32_t abs_bl(int32_t x){ int32_t m = x >> 31; return (uint32_t)((x ^ m) - m); }
// 條件累加：if (v>t) s+=v;  →  s += v & -(v>t)
static inline int32_t sum_gt_before(const int32_t*a,int n,int32_t t){ int32_t s=0; for(int i=0;i<n;i++) if(a[i]>t) s+=a[i]; return s; }
static inline int32_t sum_gt_after (const int32_t*a,int n,int32_t t){ int32_t s=0; for(int i=0;i<n;i++) s += a[i] & -(int32_t)(a[i]>t); return s; }
// ---- 3. 避免除法 ----
static inline uint32_t div10_before(uint32_t x){ return x/10; }
static inline uint32_t div10_after(uint32_t x){ return (uint32_t)(((uint64_t)x * 0xCCCCCCCDull) >> 35); } // 全 32 位元正確
static inline uint32_t mod_pow2(uint32_t x,uint32_t n_pow2){ return x & (n_pow2-1); }
// 迴圈內除同一個數：先算倒數（定點）
static inline uint32_t recip16(uint32_t d){ return 0xFFFFFFFFu / d; }                // 只算一次（d>=1）
static inline uint32_t div_by_recip(uint32_t x, uint32_t r, uint32_t d){           // 實測 32 位元 x、d 全範圍正確（含一次修正）
    uint32_t q = (uint32_t)(((uint64_t)x * r) >> 32);
    if ((q + 1) * d <= x) q++;
    return q;
}
// 取模環形索引：i = (i+1) % N  →  if(++i==N) i=0;
// ---- 4. 定點數 Q16.16 ----
typedef int32_t q16;
#define Q16(x) ((q16)((x) * 65536.0))
static inline q16 q16_mul(q16 a,q16 b){ return (q16)(((int64_t)a*b) >> 16); }
static inline q16 q16_div(q16 a,q16 b){ return (q16)(((int64_t)a << 16) / b); }
static inline float q16_to_f(q16 a){ return a / 65536.0f; }
// ---- 5. 只走訪設成 1 的位元 ----
static inline int bits_before(uint32_t m,int*out){ int k=0; for(int i=0;i<32;i++) if(m>>i&1) out[k++]=i; return k; }
static inline int bits_after(uint32_t m,int*out){ int k=0; while(m){ out[k++]=__builtin_ctz(m); m&=m-1; } return k; }  // Zbb: ctz
// ---- 6. 位元反轉 ----
static inline uint32_t rev_before(uint32_t x){ uint32_t r=0; for(int i=0;i<32;i++){ r=(r<<1)|(x&1); x>>=1; } return r; }
static inline uint32_t rev_after(uint32_t x){
    x = ((x>>1)&0x55555555u)|((x&0x55555555u)<<1);
    x = ((x>>2)&0x33333333u)|((x&0x33333333u)<<2);
    x = ((x>>4)&0x0F0F0F0Fu)|((x&0x0F0F0F0Fu)<<4);
    return __builtin_bswap32(x);                              // Zbb: rev8；Arm: rev（整個可用 rbit）
}
// ---- 7. 整數平方根（不用浮點） ----
static inline uint32_t isqrt_before(uint32_t x){ uint32_t r=0; while((uint64_t)(r+1)*(r+1)<=x) r++; return r; }
static inline uint32_t isqrt_after(uint32_t x){
    uint32_t r=0, b=1u<<30;
    while(b>x) b>>=2;
    while(b){ if(x>=r+b){ x-=r+b; r=(r>>1)+b; } else r>>=1; b>>=2; }
    return r;
}
// ---- 8. 迴圈展開 4 路 + 多個累加器（打斷相依鏈） ----
static inline int32_t sum_before(const int32_t*a,int n){ int32_t s=0; for(int i=0;i<n;i++) s+=a[i]; return s; }
static inline int32_t sum_after(const int32_t*a,int n){
    int32_t s0=0,s1=0,s2=0,s3=0; int i=0;
    for(; i+4<=n; i+=4){ s0+=a[i]; s1+=a[i+1]; s2+=a[i+2]; s3+=a[i+3]; }
    for(; i<n; i++) s0+=a[i];
    return (s0+s1)+(s2+s3);
}
// ---- 9. 小陣列排序：插入排序（n<=16 比 qsort 快，無函式指標呼叫） ----
static inline void isort(int32_t*a,int n){ for(int i=1;i<n;i++){ int32_t v=a[i]; int j=i-1; while(j>=0&&a[j]>v){ a[j+1]=a[j]; j--; } a[j+1]=v; } }
```

| 技巧 | 什麼時候用 | 什麼時候別用 |
|---|---|---|
| 查表 `POP8[]` | 函式值域小（8 位元）、重複算很多次 | RP2350 RISC-V 已有 `cpop`，直接用 builtin |
| 無分支 `x & -(cond)` | 條件隨機、分支常猜錯 | 條件幾乎總是同一邊（猜得準，分支反而快） |
| 倒數乘法 `div_by_recip` | 迴圈內一直除以**同一個變數** d | d 是常數（編譯器已處理） |
| `& (N-1)` 取代 `% N` | N 是 2 的冪 | N 不是 2 的冪 → 用 `if(++i==N) i=0;` |
| 只走訪 1 的位元 `m &= m-1` | 稀疏 bitmask | |
| 多累加器展開 | 長迴圈、相依鏈長 | 迴圈很短；程式碼大小受限 |
| 插入排序 | n ≤ 16 左右 | 大陣列 |
| 整數 `isqrt` | RISC-V 沒浮點時開根號 | Arm 有 FPU 可用 `sqrtf` |

### 4.2 定點數 Q16.16

- 乘：`(int64_t)a*b >> 16`；除：`((int64_t)a << 16) / b`。實測 `q16_mul` 在 ±100 × ±10 範圍最大誤差 0.0016。
- 小心溢位：Q16.16 整數部分只有 ±32767。

### 4.3 硬體除法器

- RP2040 有 SIO 硬體除法器，**RP2350 沒有**（SDK 的 `HAS_SIO_DIVIDER` 只對 RP2040 定義）。RP2350 兩種核心都用 CPU 的除法指令。
- Hazard3 `div`/`rem` 約 18–19 週期，`mul` 1 週期 → 迴圈內除法換乘法很值得。

### 4.4 浮點：Arm vs RISC-V

| | Arm（Cortex-M33） | RISC-V（Hazard3） |
|---|---|---|
| float | 單精度 FPU（工具鏈 `-march=armv8-m.main+fp+dsp`） | **沒有**（march 無 F/D）→ 軟體模擬，很慢 |
| double | 沒有雙精度 FPU；RP2350 有 DCP 協處理器（`HAS_DOUBLE_COPROCESSOR`），SDK 的 `pico_double` 可用它 | 軟體 |
| 建議 | `float` 可用；別寫 `1.0`（是 double），寫 `1.0f`；`x/3.0f` 改乘 `(1/3.0f)` | 熱迴圈改定點數或整數 |

選浮點實作的 CMake（SDK 原始碼）：`pico_set_float_implementation(app pico)`、`pico_set_double_implementation(app pico)`。預設就是 `pico`（SDK 註解：選目前平台最快的實作）；RP2350 上 float 另可選 `pico_vfp`（Arm FPU 指令）或 `pico_dcp`，double 可選 `pico_dcp`；`compiler` 用 C 函式庫版本，`none` 移除。兩者實際速度差 未查證。

### 4.5 插值器

每核心兩組（`interp0`、`interp1`），適合「查表位址 = base + (x >> shift & mask)」這類每筆都要做的計算。範本與 API 在 [cheatsheet.md](cheatsheet.md) 第 5.3 節。

---

## 5. 卸載與平行

### 5.1 DMA 乒乓雙緩衝（已語法檢查）

兩個通道 `chain_to` 對方：A 寫 buf0 時 CPU 處理 buf1，反之亦然。每次被觸發時傳輸次數會從 `TRANS_COUNT` 重新載入（SDK 暫存器註解），所以 ISR 只要重設寫入位址。

```c
// DMA 乒乓雙緩衝：A 寫 buf0 時 CPU 處理 buf1，兩個通道互相 chain
#include "pico/stdlib.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "hardware/pio.h"

#define N 256
static uint32_t buf[2][N];
static int ch[2];
static volatile int ready = -1;          // 剛填滿的 buffer 編號

static void __isr dma_handler(void) {
    for (int i = 0; i < 2; i++) {
        if (dma_channel_get_irq0_status(ch[i])) {
            dma_channel_acknowledge_irq0(ch[i]);
            // 重設寫入位址；下一次被 chain 觸發時從頭寫
            dma_channel_set_write_addr(ch[i], buf[i], false);
            ready = i;
        }
    }
}

void pingpong_start(PIO pio, uint sm) {
    ch[0] = dma_claim_unused_channel(true);
    ch[1] = dma_claim_unused_channel(true);
    for (int i = 0; i < 2; i++) {
        dma_channel_config c = dma_channel_get_default_config(ch[i]);
        channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
        channel_config_set_read_increment(&c, false);
        channel_config_set_write_increment(&c, true);
        channel_config_set_dreq(&c, pio_get_dreq(pio, sm, false)); // RX
        channel_config_set_chain_to(&c, ch[i ^ 1]);              // 完成後啟動另一個
        dma_channel_configure(ch[i], &c, buf[i], &pio->rxf[sm], N, false);
        dma_channel_set_irq0_enabled(ch[i], true);
    }
    irq_set_exclusive_handler(DMA_IRQ_0, dma_handler);
    irq_set_enabled(DMA_IRQ_0, true);
    dma_channel_start(ch[0]);
}

uint32_t consume(void) {
    while (ready < 0) tight_loop_contents();
    int b = ready; ready = -1;
    uint32_t s = 0;
    for (int i = 0; i < N; i++) s += buf[b][i];   // 此時另一個 buffer 正在被 DMA 寫
    return s;
}
```

### 5.2 DMA 雜項：搬移、環形、sniff、bswap（已語法檢查）

```c
// DMA 雜項：記憶體搬移、環形擷取、sniff 求和/CRC、byte swap
#include "pico/stdlib.h"
#include "hardware/dma.h"
#include "hardware/pio.h"

// 1) 大塊 memcpy 交給 DMA，CPU 同時做別的
int dma_copy_start(void *dst, const void *src, uint32_t words) {
    int ch = dma_claim_unused_channel(true);
    dma_channel_config c = dma_channel_get_default_config(ch);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
    channel_config_set_read_increment(&c, true);
    channel_config_set_write_increment(&c, true);   // 沒設 dreq：全速
    dma_channel_configure(ch, &c, dst, src, words, true);
    return ch;   // 之後 dma_channel_wait_for_finish_blocking(ch); dma_channel_unclaim(ch);
}

// 2) 環形擷取：寫入位址只在低 size_bits 位元繞圈，buffer 必須對齊 1<<size_bits 位元組
#define RING_BITS 10                         // 1024 bytes = 256 words
static uint32_t ring[256] __attribute__((aligned(1 << RING_BITS)));
int ring_capture_start(PIO pio, uint sm) {
    int ch = dma_claim_unused_channel(true);
    dma_channel_config c = dma_channel_get_default_config(ch);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
    channel_config_set_read_increment(&c, false);
    channel_config_set_write_increment(&c, true);
    channel_config_set_ring(&c, true, RING_BITS);   // true = 寫入端繞圈
    channel_config_set_dreq(&c, pio_get_dreq(pio, sm, false));
    dma_channel_configure(ch, &c, ring, &pio->rxf[sm], 0xFFFFFFF, true); // 28-bit 上限
    return ch;
}
// 目前寫到哪：(dma_hw->ch[ch].write_addr - (uintptr_t)ring) / 4

// 3) sniff 加總：搬移的同時免費算出總和（SUM 模式）
uint32_t dma_sum(const uint32_t *src, uint32_t words) {
    static uint32_t sink;
    int ch = dma_claim_unused_channel(true);
    dma_channel_config c = dma_channel_get_default_config(ch);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
    channel_config_set_read_increment(&c, true);
    channel_config_set_write_increment(&c, false);
    channel_config_set_sniff_enable(&c, true);
    dma_sniffer_set_data_accumulator(0);
    dma_sniffer_enable(ch, DMA_SNIFF_CTRL_CALC_VALUE_SUM, true);
    dma_channel_configure(ch, &c, &sink, src, words, true);
    dma_channel_wait_for_finish_blocking(ch);
    uint32_t s = dma_sniffer_get_data_accumulator();
    dma_sniffer_disable();
    dma_channel_unclaim(ch);
    return s;
}

// 4) 搬移時順便反轉位元組順序（大小端轉換）
void set_bswap(dma_channel_config *c) { channel_config_set_bswap(c, true); }
```

| API | 用途 |
|---|---|
| `channel_config_set_ring(&c, write, size_bits)` | 位址只在低 `size_bits` 位元繞圈；buffer 對齊 `1<<size_bits` |
| `channel_config_set_sniff_enable(&c, true)` + `dma_sniffer_enable(ch, mode, true)` | 搬移時順便算：`DMA_SNIFF_CTRL_CALC_VALUE_CRC32`、`_CRC32R`、`_CRC16`、`_CRC16R`、`_EVEN`、`_SUM` |
| `dma_sniffer_set_data_accumulator(seed)` / `dma_sniffer_get_data_accumulator()` | 設初值 / 讀結果 |
| `dma_sniffer_set_output_reverse_enabled`、`_output_invert_enabled`、`_byte_swap_enabled`、`dma_sniffer_disable` | CRC 輸出調整 |
| `channel_config_set_bswap(&c, true)` | 搬移時反轉位元組順序 |
| `channel_config_set_high_priority(&c, true)` | DMA 內部排程優先 |
| `dma_start_channel_mask(mask)` | 同時啟動多個通道 |
| `dma_encode_transfer_count_with_self_trigger(n)`、`dma_encode_endless_transfer_count()` | RP2350 傳輸次數模式（**SDK 2.2.0 起**） |

pico-examples `dma/sniff_crc` 用 `CRC32R` + `output_reverse` + 初值 `0xffffffff`，資料後面附上 CRC 時結果為 0 代表正確。

### 5.3 PIO：用 autopull 與 side-set 省指令

```pio
.program par_before
; 每次手動 pull，迴圈 3 指令/位元組
.wrap_target
    pull block
    out pins, 8
    out null, 24
.wrap
```

```pio
.program par_after
.side_set 1
; autopull 門檻 8：每個 out 吐 8 位元，1 指令/位元組，側寫時脈腳
.wrap_target
    out pins, 8   side 0
    nop           side 1
.wrap
% c-sdk {
static inline void par_after_init(PIO pio, uint sm, uint off, uint pin0, uint clk, float div) {
    pio_sm_config c = par_after_program_get_default_config(off);
    sm_config_set_out_pins(&c, pin0, 8);
    sm_config_set_sideset_pins(&c, clk);
    sm_config_set_out_shift(&c, true, true, 8);      // 右移、autopull、門檻 8
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX);   // TX FIFO 4→8 層
    sm_config_set_clkdiv(&c, div);
    for (uint i = 0; i < 8; i++) pio_gpio_init(pio, pin0 + i);
    pio_gpio_init(pio, clk);
    pio_sm_set_consecutive_pindirs(pio, sm, pin0, 8, true);
    pio_sm_set_consecutive_pindirs(pio, sm, clk, 1, true);
    pio_sm_init(pio, sm, off, &c);
    pio_sm_set_enabled(pio, sm, true);
}
%}
```

```c
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "par_after.pio.h"
void go(void) {
    PIO pio; uint sm, off;
    // 2.0.0+ 有：自動挑一個裝得下的 PIO 與 SM
    pio_claim_free_sm_and_add_program_for_gpio_range(&par_after_program, &pio, &sm, &off, 2, 9, true);
    par_after_init(pio, sm, off, 2, 10, 1.0f);
    pio_sm_put_blocking(pio, sm, 0xA5);
}
```

| 技巧 | 效果 |
|---|---|
| `sm_config_set_out_shift(&c, true, true, 8)` autopull | 省掉 `pull` 指令 |
| `sm_config_set_in_shift(&c, right, true, 32)` autopush | 省掉 `push` |
| `.side_set N` | 同一指令順便改時脈腳，不用另外 `set` |
| `sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX)` | 單向時 FIFO 4 → 8 層，少等 |
| `sm_config_set_clkdiv(&c, 1.0f)` | 全速 |
| `out pins, 8` 一次多位元 | 比逐位元快 8 倍 |
| `.wrap` | 迴圈不用 `jmp` |
| `pio_claim_free_sm_and_add_program_for_gpio_range` | 自動挑 PIO 與 SM（2.0.0 起） |

### 5.4 IRQ 還是輪詢

- **輪詢**（`while(!done)`、`dma_channel_wait_for_finish_blocking`）：延遲最低，但 CPU 被綁住。等待時間很短或沒別的事可做時用。
- **IRQ**：CPU 可以做別的事；每次中斷有進出成本，事件很頻繁時（例如每個字）反而更慢 → 改用 DMA 搬一大塊，完成才中斷一次。
- ISR 要短：只清旗標、記狀態，計算留給主迴圈。ISR 本體放 SRAM（`__not_in_flash_func`）。

### 5.5 雙核心切工作（已語法檢查）

```c
// 雙核心切工作：core1 算後半，用 multicore FIFO 傳起訖與結果
#include "pico/stdlib.h"
#include "pico/multicore.h"

#define LEN 4096
static int32_t a[LEN];

static int64_t sum_range(uint32_t lo, uint32_t hi) {
    int64_t s = 0;
    for (uint32_t i = lo; i < hi; i++) s += a[i];
    return s;
}
static void core1_main(void) {
    while (true) {
        uint32_t lo = multicore_fifo_pop_blocking();
        uint32_t hi = multicore_fifo_pop_blocking();
        int64_t s = sum_range(lo, hi);
        multicore_fifo_push_blocking((uint32_t)s);
        multicore_fifo_push_blocking((uint32_t)(s >> 32));
    }
}
void split_init(void) { multicore_launch_core1(core1_main); }

int64_t sum_parallel(void) {
    multicore_fifo_push_blocking(LEN / 2);
    multicore_fifo_push_blocking(LEN);
    int64_t s0 = sum_range(0, LEN / 2);             // core0 同時算前半
    uint32_t lo = multicore_fifo_pop_blocking();
    uint32_t hi = multicore_fifo_pop_blocking();
    return s0 + (int64_t)(((uint64_t)hi << 32) | lo);
}
```

- FIFO 每格 32 位元，傳「起訖索引」不要傳整批資料；資料本身放共用 SRAM。
- 兩核心各寫不同陣列區段，避免同一個 bank 互搶與鎖。

### 5.6 無鎖 SPSC 環形佇列（已語法檢查）

```c
// 單生產者單消費者無鎖環形佇列（core0 寫、core1 讀），容量必須是 2 的次方
#include "pico/stdlib.h"
#include "hardware/sync.h"

#define QCAP 64u
typedef struct {
    volatile uint32_t head;      // 只有生產者寫
    volatile uint32_t tail;      // 只有消費者寫
    uint32_t data[QCAP];
} spsc_t;

static inline bool spsc_push(spsc_t *q, uint32_t v) {
    uint32_t h = q->head;
    if (h - q->tail == QCAP) return false;         // 滿
    q->data[h & (QCAP - 1)] = v;
    __mem_fence_release();                          // 先寫資料再發布 head
    q->head = h + 1;
    return true;
}
static inline bool spsc_pop(spsc_t *q, uint32_t *v) {
    uint32_t t = q->tail;
    if (q->head == t) return false;                 // 空
    __mem_fence_acquire();                          // 先看到 head 再讀資料
    *v = q->data[t & (QCAP - 1)];
    __mem_fence_release();
    q->tail = t + 1;
    return true;
}
spsc_t g_q;
```

- 只能**一個**生產者、**一個**消費者；多對多改用 `pico/util/queue.h`（`queue_init`、`queue_try_add`、`queue_add_blocking`、`queue_try_remove`、`queue_remove_blocking`、`queue_get_level`…，內部用 spin lock）。
- 屏障：`__mem_fence_release()` 先寫資料再發布索引，`__mem_fence_acquire()` 先讀索引再讀資料；SDK 另有 `__dmb()`、`__dsb()`、`__isb()`、`__sev()`、`__wfe()`。

### 5.7 臨界區越短越好

- `critical_section_enter_blocking` / `spin_lock_blocking` 內只放「讀改寫共用變數」那一行，運算移到鎖外。
- 每核心各自累加、最後合併一次，比每次都上鎖快很多（第 7 節實測同樣道理）。

---

## 6. 時脈與電壓

```c
// 超頻：先升電壓再升時脈；周邊時脈要另外確認
#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "hardware/vreg.h"

bool overclock(uint32_t khz) {
    vreg_set_voltage(VREG_VOLTAGE_1_20);   // 預設 1.10V；最高 VREG_VOLTAGE_MAX(1.30V)
    sleep_ms(10);                           // 等電壓穩定
    if (!set_sys_clock_khz(khz, false))     // false：做不到就回傳 false 不 assert
        return false;
    stdio_init_all();                        // clk_peri 若改變，UART 鮑率要重設
    return clock_get_hz(clk_sys) == khz * 1000u;
}
```

| API / 常數 | 說明 |
|---|---|
| `SYS_CLK_HZ` 預設 150000000 | RP2350 預設 150 MHz |
| `set_sys_clock_khz(khz, required)` | `required=true` 做不到會 assert；`false` 回傳 bool |
| `set_sys_clock_hz(hz, required)`、`set_sys_clock_pll(vco, pd1, pd2)`、`set_sys_clock_48mhz()` | |
| `check_sys_clock_khz(khz, &vco, &pd1, &pd2)` | 先查做不做得到 |
| `src/rp2_common/hardware_clocks/scripts/vcocalc.py` | SDK 附的 PLL 參數計算腳本 |
| `vreg_set_voltage(VREG_VOLTAGE_1_20)`、`vreg_get_voltage()` | 預設 `VREG_VOLTAGE_DEFAULT` = 1.10 V，上限 `VREG_VOLTAGE_MAX` = 1.30 V |
| `vreg_disable_voltage_limit()` | 解除 1.30 V 上限（危險，考試不要） |
| `clock_get_hz(clk_sys)`、`clock_get_hz(clk_peri)` | 確認實際頻率 |

- **周邊時脈（已對 SDK 2.3.1 `clocks.c`）**：開機時 `clk_peri` 接 `clk_sys`（150 MHz）。`set_sys_clock_khz` → `set_sys_clock_pll` 會把 `clk_peri` 改接 **48 MHz 的 USB PLL**（`PICO_CLOCK_ADJUST_PERI_CLOCK_WITH_SYS_CLOCK` 預設 0）。所以 `clk_peri` 一定會變，UART / SPI 要重新 `stdio_init_all()` / `uart_init`，用 `clock_get_hz(clk_peri)` 確認。
- **PIO、PWM 吃的是 `clk_sys`**：超頻後 PIO 的 `sm_config_set_clkdiv` 與 PWM 分頻要用新的 `clock_get_hz(clk_sys)` 重算，不然 PIO 協定的速度會跟著變快。
- **`sleep_ms`、`time_us_64` 不受影響（已對 SDK 原始碼）**：計時器的 tick 由 `clk_ref` 產生（`runtime_init_clocks.c` 的 `start_all_ticks`），`clk_ref` 一直接 12 MHz XOSC，`set_sys_clock_pll` 也把它設回 XOSC。
- **`set_sys_clock_khz` 不會自己調電壓**：要先 `vreg_set_voltage`。PLL 的 VCO 範圍 750–1600 MHz（`pll.h`），做得到的例子：200000、240000、250000、300000 kHz；做不到時 `false` 版回傳 false。
- 官方保證的最高時脈、各電壓對應的安全頻率：**未查證**（只在 datasheet）。考試計時題先確認規則允不允許超頻。

---

## 7. Linux pthread

### 7.1 偽共享、填充、mutex vs atomic（4 vCPU VM 實測）

```c
// 偽共享 vs 快取行填充；mutex vs atomic；綁核心。編譯：gcc -O2 -pthread fs.c
#define _GNU_SOURCE
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdint.h>
#include <time.h>
#define T 4
#define ITERS 20000000L
#define CACHELINE 64

struct bad  { volatile long v; };                                   // 4 個計數器擠在同一條快取行
struct good { _Alignas(CACHELINE) volatile long v; };               // 每個獨占一條快取行
static struct bad  B[T];
static struct good G[T];
static long shared_plain; static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
static atomic_long shared_atomic;

static double now(void){ struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return t.tv_sec+t.tv_nsec*1e-9; }
static void pin(int cpu){ cpu_set_t s; CPU_ZERO(&s); CPU_SET(cpu,&s); pthread_setaffinity_np(pthread_self(),sizeof s,&s); }

static void *w_bad(void*a){ long i=(long)a; pin(i); for(long k=0;k<ITERS;k++) B[i].v++; return 0; }
static void *w_good(void*a){ long i=(long)a; pin(i); for(long k=0;k<ITERS;k++) G[i].v++; return 0; }
static void *w_local(void*a){ long i=(long)a; pin(i); long s=0; for(long k=0;k<ITERS;k++) s++; G[i].v=s; return 0; } // 區域變數最後才寫回
static void *w_mutex(void*a){ (void)a; for(long k=0;k<ITERS/10;k++){ pthread_mutex_lock(&mu); shared_plain++; pthread_mutex_unlock(&mu);} return 0; }
static void *w_atomic(void*a){ (void)a; for(long k=0;k<ITERS/10;k++) atomic_fetch_add_explicit(&shared_atomic,1,memory_order_relaxed); return 0; }

static double run(void*(*f)(void*)){ pthread_t th[T]; double t0=now(); for(long i=0;i<T;i++) pthread_create(&th[i],0,f,(void*)i); for(int i=0;i<T;i++) pthread_join(th[i],0); return now()-t0; }
int main(void){
    printf("false sharing : %.3fs\n", run(w_bad));
    printf("padded        : %.3fs\n", run(w_good));
    printf("local+writeback: %.3fs\n", run(w_local));
    double tm = run(w_mutex);  printf("mutex  : %.3fs total=%ld\n", tm, shared_plain);
    double ta = run(w_atomic); printf("atomic : %.3fs total=%ld\n", ta, (long)shared_atomic);
}
```

實測輸出（每執行緒 2000 萬次遞增；mutex/atomic 每執行緒 200 萬次）：

```
false sharing : 0.049s
padded        : 0.007s     ← 每個計數器獨占 64 bytes，快約 7 倍
local+writeback: 0.000s    ← 區域變數最後寫回，最快
mutex  : 0.353s total=8000000
atomic : 0.400s total=8000000   ← 高度搶用時 atomic 不一定比 mutex 快
```

| 招式 | 重點 |
|---|---|
| 快取行填充 | `_Alignas(64)`（C11）/ `alignas(64)`（C++）；或 `std::hardware_destructive_interference_size`（`<new>`，C++17；實測 g++ 13 = 64，**clang 18 編譯失敗**，所以直接寫 `alignas(64)` 最保險） |
| 區域累加後合併 | 共用變數最快的寫法是「不要共用」 |
| atomic vs 鎖 | 單一計數器用 `atomic_fetch_add_explicit(..., memory_order_relaxed)`；保護多個欄位用 mutex |
| spin vs mutex | 臨界區極短且執行緒數 ≤ 核心數才用 spin（`pthread_spin_lock`）；否則 mutex（會睡，不浪費 CPU） |
| 綁核心 | `pthread_setaffinity_np`（需 `#define _GNU_SOURCE`）；命令列 `taskset -c 0-3 ./app` |
| `-march=native` | 對本機 CPU 最佳化；**OJ 機器 CPU 不同時可能非法指令**，不確定就別用。GCC 13 實測：`-O2 -march=native` 對未知長度迴圈仍未向量化，要 `-O3` |
| memcpy | GCC 13 `-O2` 會自動把複製迴圈換成 `memcpy`（實測）；手寫逐 byte 迴圈不會比較快 |
| 執行緒數 | `nproc` / `sysconf(_SC_NPROCESSORS_ONLN)`；多開沒用 |

---

## 9. 叫考場小模型優化用的 prompt

先貼 [cheatsheet.md](cheatsheet.md) 第 9 節的基本 prompt，再接這段：

```text
你是嵌入式效能工程師。目標：讓下面的函式在 RP2350（{rp2350-riscv 或 rp2350-arm-s}，150 MHz）跑得更快，輸出結果必須與原版逐位元相同。

【限制】
- 只能使用下面「允許清單」裡的函式、巨集、型別；清單外的名稱一律不准出現。不確定就說「不確定」，不要猜。
- 一次只提出「一個」修改。每個修改要說：改了哪幾行、為什麼會變快、預期影響（例如少一次除法、少一次 flash 讀取）。
- RISC-V 版本沒有浮點硬體、沒有硬體除法器（div 約 18 週期），也沒有 SIO 除法器。Arm 版本只有單精度 FPU。
- 不要改函式簽名、不要改輸入輸出格式。
- 保留原版函式，新版本取名 xxx_fast，並寫一段比對兩者結果的測試。

【允許清單】
{從 cheatsheet.md / optimization.md 複製相關 API 表，例如第 2.2、5.2 節的表}

【量測方式】
用 time_us_64() 包住呼叫，跑 20 次取最小值。

【原始程式】
{貼程式}

【我量到的數字】
{例：原版 1830 us}

請只給：1) 一個修改的 diff  2) 理由  3) 比對測試。不要給其他建議。
```

每輪用法：模型給一個修改 → 自己編譯 → 跑比對測試（結果要一樣）→ 量時間 → 有變快才保留 → 把新數字貼回去要下一個修改。模型提到的每個名稱，用 `Ctrl+F` 在這兩份筆記搜尋，搜不到就不要用。

---

## 10. 未查證清單

- RP2350 datasheet 的數字：最高額定時脈、各電壓對應頻率、Cortex-M33 `udiv` 週期數、DCP 與 VFP 實際速度、匯流排仲裁細節。
- Arm DWT `dwt_cyccnt` 在 RP2350 實機可用（暫存器存在於 SDK 標頭，沒上機測）。
- LTO 支援（SDK cmake 裡找不到）。
- Hazard3 週期數取自 Hazard3 repo（假設最高效能設定），RP2350 實際值以 datasheet 為準。
