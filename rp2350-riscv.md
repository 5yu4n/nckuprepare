# RP2350 RISC-V（Hazard3）冷門知識包

> 晶片確定是 RP2350；板子可能是 Pico 2、Pico 2 W 或其他廠商的 RP2350 板。section2-2 附 RISC-V 指令集手冊，所以很可能跑在 RISC-V（Hazard3）模式。
> **來源**：pico-sdk 2.3.1 原始碼（工具鏈參數、標頭、板子定義）與 Hazard3 官方文件（`doc/sections/*.adoc`）。指令與編碼都用 clang 18 對 SDK 的 `-march` 實際組譯確認過；`rv_tools.h` 的 C 備援版本在電腦上用 100 萬筆隨機輸入驗證過。RP2350 datasheet 抓不到，只在 datasheet 裡的事一律標 **未查證**。
> 相關檔案：[00-START.md](00-START.md)、[build-and-debug.md](build-and-debug.md)、[cheatsheet.md](cheatsheet.md)、[solutions.md](solutions.md)（S16 手寫組語）、[optimization.md](optimization.md)、[prompts.md](prompts.md)。

## 0. 十秒版

- 兩種核心開機時二選一：2 × Cortex-M33（`rp2350-arm-s`）或 2 × Hazard3 RISC-V（`rp2350-riscv`）。程式要用對應的 `PICO_PLATFORM` 重新編譯。
- Hazard3 有：整數乘法 1 週期、Zba/Zbb/Zbs/Zbkb 位元指令都 1 週期、原子指令（A）。
- Hazard3 沒有：**浮點硬體**、向量指令、SIO 硬體除法器（RP2350 兩種架構都沒有）。除法 `div`/`divu` 約 18 週期。
- **不對齊的 load/store 會觸發例外**（Arm M33 可以）。SDK 編譯時加 `-mstrict-align`。
- 週期計數器 `mcycle` / `minstret` 重置後是**停的**，要先清 `mcountinhibit`。
- 分支預測只記得「最後一個往回跳的分支」→ 最內層迴圈要單純，常見情況寫在不跳的那邊。

---

## 1. 我在哪種板子、哪種架構上？

### 編譯時

| 想知道 | 怎麼看 | 來源 |
|---|---|---|
| 架構 | `#if defined(__riscv)`（編譯器內建）或 `#if PICO_RISCV`（SDK 的 CMake 在 RISC-V 時定義為 1） | SDK `src/rp2350/pico_platform/CMakeLists.txt` |
| 是不是 RP2350 | `#if PICO_RP2350` | 同上 |
| 板子名稱 | `PICO_BOARD` 是字串巨集，可以 `printf("%s\n", PICO_BOARD);` | SDK `src/common/pico_base_headers/CMakeLists.txt` |
| flash 大小 | `PICO_FLASH_SIZE_BYTES`（板子標頭給；`pico2.h`、`pico2_w.h` 都是 4 MB） | SDK `src/boards/include/boards/` |
| A 還是 B 封裝 | `PICO_RP2350A`：1 = 30 個 GPIO，0 = 48 個 GPIO；`NUM_BANK0_GPIOS` 會跟著是 30 或 48 | SDK `platform_defs.h`、`platform.h` |

**坑**：沒有板子標頭時 SDK 預設 `PICO_RP2350A` 是 **0**（48 腳）。用其他廠商的板子要設對 `PICO_BOARD`，否則腳位數、flash 大小都可能錯。SDK 2.3.1 內建 78 個 RP2350 板子標頭（`ls pico-sdk/src/boards/include/boards`）。

### CMake 參數

```sh
cmake -DPICO_PLATFORM=rp2350-riscv -DPICO_BOARD=pico2 ..     # RISC-V
cmake -DPICO_PLATFORM=rp2350-arm-s -DPICO_BOARD=pico2 ..     # Arm
```

`PICO_BOARD` 和 `PICO_PLATFORM` 衝突時 CMake 會直接報錯（`pico_pre_load_platform.cmake`）。考場若給了 CMakeLists 或建置指令，**照題目的，不要改**。

### 執行時量 flash 大小（選用）

SDK 範例 `flash/nuke` 的做法：送 JEDEC 0x9F 指令，容量 = `1u << rxbuf[3]`。

```c
#include "hardware/flash.h"
uint8_t tx[4] = {0x9f}, rx[4] = {0};
flash_do_cmd(tx, rx, 4);                 // 呼叫時另一顆核心不能在跑 flash 上的程式
uint32_t flash_bytes = 1u << rx[3];
```

一般題目用不到，`PICO_FLASH_SIZE_BYTES` 就夠了。

---

## 2. Hazard3 在 RP2350 上有哪些擴充

SDK 工具鏈（`cmake/preload/toolchains/pico_riscv_gcc.cmake`）依序嘗試：

1. `-mcpu=hazard3-rp2350`
2. `-march=rv32ima_zicsr_zifencei_zba_zbb_zbs_zbkb_zca_zcb_zcmp -mabi=ilp32`
3. `-march=rv32imac_zicsr_zifencei_zba_zbb_zbs_zbkb -mabi=ilp32`（舊工具鏈）

再加上 `-mstrict-align`（除非 `PICO_NO_STRICT_ALIGN=1`）。

| 擴充 | 內容 | 在 RP2350 上 |
|---|---|---|
| I | 基本整數 | 有 |
| M | 乘除法 | 有：`mul`/`mulh*` 1 週期，`div`/`rem` 18–19 週期 |
| A | 原子（`lr.w`/`sc.w`/`amo*.w`） | 有：`amo*` 每次嘗試 4 週期 |
| C（Zca）、Zcb、Zcmp | 16 位元壓縮指令、`cm.push`/`cm.pop` | 有（縮小程式碼，速度和 32 位元版相同） |
| Zba | `sh1add`/`sh2add`/`sh3add` | 有 |
| Zbb | `clz` `ctz` `cpop` `min` `max` `minu` `maxu` `andn` `orn` `xnor` `rol` `ror` `rori` `rev8` `orc.b` `sext.b` `sext.h` `zext.h` | 有 |
| Zbs | `bset` `bclr` `binv` `bext`（含 `…i` 立即值版） | 有 |
| Zbkb | `pack` `packh` `brev8` `zip` `unzip` | 有 |
| Xh3bextm | `h3.bextm`/`h3.bextmi`：一次取出 1–8 個連續位元 | 有（SDK `features.h`） |
| Xh3power | `h3.block`/`h3.unblock`（SDK 的 `__wfe`/`__sev` 在 RISC-V 就是它們） | 有 |
| Xh3irq | Hazard3 自己的中斷控制器 | 有 |
| F / D | 浮點 | **沒有** |
| V | 向量 | **沒有** |
| Zbc | `clmul` | Hazard3 文件列了，但 **SDK 的 `-march` 沒開**，不要用 |
| Zilsd | `ld`/`sd` 成對存取 | Hazard3 文件列了，**SDK 沒開**，不要用 |

週期數來自 Hazard3 文件，文件寫的前提是「`MULDIV_UNROLL = 2`、`MUL_FAST = 1`、其他選項全開最快」。**RP2350 實際用的設定未查證**（datasheet 才有）。

---

## 3. 好用指令表（全部 1 週期）

「C 寫法」欄的寫法用 clang 對 SDK 的 `-march` 以 `-O2` 編譯，實際產生右邊那條指令（只有一條，不含 `ret`）。GCC 一般也會，但沒在 RISC-V GCC 上確認（環境沒有）。

| 想做的事 | C 寫法（編譯器自動產生） | 指令 |
|---|---|---|
| 1 的個數 | `__builtin_popcount(x)` | `cpop` |
| 前導 0 個數（x ≠ 0） | `__builtin_clz(x)` | `clz` |
| 尾端 0 個數（x ≠ 0） | `__builtin_ctz(x)` | `ctz` |
| 位元組反轉（大小端） | `__builtin_bswap32(x)` | `rev8` |
| `a & ~b` / `a \| ~b` / `~(a ^ b)` | 直接寫 | `andn` / `orn` / `xnor` |
| 最小 / 最大（有號） | `a < b ? a : b` | `min` / `max` |
| 最小 / 最大（無號） | 同上，用 `uint32_t` | `minu` / `maxu` |
| 符號延伸 8 / 16 位元 | `(int8_t)x`、`(int16_t)x` | `sext.b` / `sext.h` |
| 取低 16 位元 | `(uint16_t)x` | `zext.h` |
| 循環移位 | `(x << (k & 31)) \| (x >> ((32 - k) & 31))` | `rol`（常數時 `rori`） |
| 陣列位址 `a[i]` | `a[i]`（`uint32_t` 陣列） | `sh2add` + `lw` |
| `(a << 1) + b` | 直接寫 | `sh1add` |
| 設 / 清 / 反轉 / 取第 i 位元 | `x \| (1u << (i & 31))` 等 | `bset` / `bclr` / `binv` / `bext` |
| 兩個 byte 拼成 16 位元 | `(a & 0xff) \| ((b & 0xff) << 8)` | `packh` |
| 兩個 16 位元拼成 32 位元 | `(a & 0xffff) \| (b << 16)` | `pack` |
| 除以常數 | `x / 10` | `mulhu` + `srli`（不會用 `divu`） |

**編譯器不會自己產生的**（要用行內組語，見第 4 節）：

| 指令 | 作用 | 用途 |
|---|---|---|
| `brev8` | 每個 byte 內部位元反轉 | 32 位元完整反轉 = `brev8` + `rev8` 兩條（C 寫法要約 15 條） |
| `orc.b` | 每個 byte：非 0 → `0xFF`，0 → `0x00` | 一次檢查 4 個 byte 有沒有 0（`strlen`、`memchr`） |
| `zip` / `unzip` | 低半與高半位元交錯 / 拆開 | Morton 碼、位元平面轉換 |
| `h3.bextm` | 從暫存器取出連續 1–8 位元 | 解封包、取欄位（一般 `>>` + `&` 也只要 2 條） |

---

## 4. 行內組語範本：`rv_tools.h`

直接 `#include "rv_tools.h"`。RISC-V 用真指令；Arm 或電腦上自動換成等效的 C，所以同一份程式兩邊都能編。已用 SDK 標頭對 RISC-V 與 Arm 兩種目標編譯檢查；RISC-V 上 `rv_bitrev32` 編成 `brev8` + `rev8`、`rv_first_zero_byte` 編成 `orc.b` + `not` + `ctz` + `srli`。

```c
// rv_tools.h：RP2350 Hazard3 專用指令的包裝 + 週期計數。Arm 或電腦上自動改用等效 C。
#pragma once
#include <stdint.h>

#if defined(__riscv)
// ---- 編譯器不會自己產生的 Zbb/Zbkb 指令 ----
static inline uint32_t rv_brev8(uint32_t x)  { uint32_t r; __asm__("brev8 %0, %1" : "=r"(r) : "r"(x)); return r; } // 每個 byte 內位元反轉
static inline uint32_t rv_orc_b(uint32_t x)  { uint32_t r; __asm__("orc.b %0, %1" : "=r"(r) : "r"(x)); return r; } // 非 0 的 byte → 0xFF
static inline uint32_t rv_zip(uint32_t x)    { uint32_t r; __asm__("zip %0, %1"   : "=r"(r) : "r"(x)); return r; } // 低半與高半位元交錯
static inline uint32_t rv_unzip(uint32_t x)  { uint32_t r; __asm__("unzip %0, %1" : "=r"(r) : "r"(x)); return r; } // zip 的反運算
// ---- 週期 / 指令計數（M-mode CSR；SDK 程式本來就在 M-mode）----
static inline void     rv_counters_on(void)  { __asm__ volatile("csrci mcountinhibit, 5"); } // 重置後預設是停的
static inline uint32_t rv_cycles(void)       { uint32_t r; __asm__ volatile("csrr %0, mcycle"   : "=r"(r)); return r; }
static inline uint32_t rv_instret(void)      { uint32_t r; __asm__ volatile("csrr %0, minstret" : "=r"(r)); return r; }
#else
static inline uint32_t rv_brev8(uint32_t x) {
    x = ((x >> 1) & 0x55555555u) | ((x & 0x55555555u) << 1);
    x = ((x >> 2) & 0x33333333u) | ((x & 0x33333333u) << 2);
    return ((x >> 4) & 0x0F0F0F0Fu) | ((x & 0x0F0F0F0Fu) << 4);
}
static inline uint32_t rv_orc_b(uint32_t x) {
    uint32_t r = 0;
    for (int i = 0; i < 32; i += 8) if ((x >> i) & 0xFFu) r |= 0xFFu << i;
    return r;
}
static inline uint32_t rv_zip(uint32_t x) {
    uint32_t r = 0;
    for (int i = 0; i < 16; i++) r |= ((x >> i) & 1u) << (2 * i) | ((x >> (i + 16)) & 1u) << (2 * i + 1);
    return r;
}
static inline uint32_t rv_unzip(uint32_t x) {
    uint32_t r = 0;
    for (int i = 0; i < 16; i++) r |= ((x >> (2 * i)) & 1u) << i | ((x >> (2 * i + 1)) & 1u) << (i + 16);
    return r;
}
static inline void     rv_counters_on(void) {}
static inline uint32_t rv_cycles(void)  { return 0; }   // Arm 上改用 time_us_64()
static inline uint32_t rv_instret(void) { return 0; }
#endif

static inline uint32_t rv_bitrev32(uint32_t x) { return __builtin_bswap32(rv_brev8(x)); }   // RISC-V：brev8 + rev8 兩條
// 第一個 0 byte 的位置（0–3），沒有就回傳 4：strlen / memchr 一次看 4 個 byte
static inline uint32_t rv_first_zero_byte(uint32_t w) {
    uint32_t m = ~rv_orc_b(w);
    return m ? (uint32_t)__builtin_ctz(m) >> 3 : 4;
}

// 用法：RV_MEASURE("名稱", 程式碼); 印出週期數與指令數（IPC 越接近 1 越好）
#define RV_MEASURE(name, code) do { \
    rv_counters_on(); \
    uint32_t c0_ = rv_cycles(), i0_ = rv_instret(); \
    code; \
    uint32_t c1_ = rv_cycles(), i1_ = rv_instret(); \
    printf("%s: %lu cycles, %lu instr\n", name, (unsigned long)(c1_ - c0_), (unsigned long)(i1_ - i0_)); \
} while (0)
```

**`h3.bextm`**：SDK 已經有 C 巨集 `__hazard3_bextm(nbits, rs1, rs2)` 與 `__hazard3_bextmi(nbits, rs1, shamt)`（`#include "hardware/hazard3.h"`），直接用。
**坑**：SDK 2.3.1 給組語（`.S`）用的 `h3.bextm` 巨集寫的 funct3 是 `0x4`，但 Hazard3 文件與 SDK 自己的 C 巨集都是 `0`（`0x4` 是 `h3.bextmi`）。**在 `.S` 裡不要用 SDK 的 `h3.bextm` 巨集**，要用就直接寫 `.insn r 0x0b, 0, (((N - 1) & 7) << 1), rd, rs1, rs2`。

**一般原則**：能用 C 寫法讓編譯器產生指令就不要寫組語（第 3 節的表都是一條指令）。行內組語會擋住編譯器的排程與常數傳遞。

---

## 5. 計時：週期計數器

| CSR | 位址 | 內容 |
|---|---|---|
| `mcycle` / `mcycleh` | 0xB00 / 0xB80 | 64 位元週期數 |
| `minstret` / `minstreth` | 0xB02 / 0xB82 | 64 位元已執行指令數 |
| `mcountinhibit` | 0x320 | 第 0 位停 `mcycle`、第 2 位停 `minstret`，**重置值是 1（停）** |
| `mhpmcounter3..31` | — | 固定是 0，沒有其他效能計數器 |

- SDK 程式在 M-mode 執行，可以直接讀。SDK 原始碼裡**沒有任何地方**清 `mcountinhibit`，所以要自己清：`csrci mcountinhibit, 5`（`rv_counters_on()`）。開機 ROM 會不會先清掉 **未查證**，自己清最保險。
- 週期數 ÷ 時脈 = 時間（預設 150 MHz）。量很短的程式碼用 `mcycle`，量長時間用 `time_us_64()`（兩種架構都能用）。
- 指令數 ÷ 週期數 = IPC，Hazard3 最多 1。IPC 明顯低於 1 → 有 load-use 停頓、分支預測錯、除法或匯流排等待（例如在跑 flash 上的程式）。
- SDK 也有 `hardware/riscv.h` 的 `riscv_read_csr(mcycle)` 等巨集。
- Arm 上沒有 `mcycle`；Arm 的計數方式另見 [cheatsheet.md](cheatsheet.md)。

---

## 6. 讓 Hazard3 跑得快的規則

全部來自 Hazard3 文件的週期表：

1. **load 結果不要下一條就用**：`lw` 後緊接著用到結果會多 1 週期。迴圈展開、先載入下一筆再算這一筆。
2. **分支預測只記一個分支**：前端只記得「最後一個被執行、往回跳的分支」，下次預測它會跳；其他分支一律預測不跳。預測錯多 1 週期。
   - 巢狀迴圈：只有最內層受益 → 能攤平成單層就攤平，或內層展開。
   - `if` 裡面：常見情況寫在「不跳」的那邊，可用 `__builtin_expect(cond, 1)`。
   - 能不用分支就不用：用 `min`/`max` 指令，或 `-(cond) & a` 這類寫法。
3. **跳到沒對齊 4 byte 的 32 位元指令多 1 週期**（壓縮指令讓這種情況常見）：手寫組語時在熱迴圈的標籤前加 `.p2align 2`。
4. **除法 18 週期**：常數除法編譯器會換乘法；變數除法若除數重複使用，先算倒數再用 `mulhu`（見 [optimization.md](optimization.md)）；2 的次方用移位。
5. **沒有 FPU**：`float` 一次乘法是函式呼叫。SDK 在 RISC-V 上單精度用手寫 Hazard3 組語版（`float_single_hazard3.S`），雙精度用編譯器內建軟體版（`pico_double` 在 RISC-V 預設 `compiler`）。熱迴圈改**定點數**（Q15/Q16）。
6. **原子指令一次 4 週期起跳**，`amoadd.w` 遇到競爭會重試。能本地累加就不要每次 atomic。
7. **`cm.push`/`cm.pop` 是 1 + 暫存器數週期**：小函式加 `static inline` 省掉呼叫開銷。

---

## 7. 沒有的東西 / 從 Arm 搬過來會壞的地方

| Arm（M33）上的寫法 | RISC-V 上 | 改成 |
|---|---|---|
| `float` 運算（M33 有單精度 FPU） | 變成軟體函式，慢很多 | 定點整數 |
| DCP（雙精度協處理器）、`pico_float_pico_dcp`/`vfp` | 只有 Arm 能用 | 不用 |
| 不對齊存取：`*(uint32_t *)(buf + 1)` | **Load/Store address misaligned 例外**（mcause 4 / 6） | `memcpy(&v, buf + 1, 4)` 或逐 byte 組 |
| CMSIS、`__SMLAD`、`__QADD` 等 DSP/SIMD intrinsics、`core_cm33.h` | 不存在 | 一般 C；需要飽和運算就用 `min`/`max` 夾住 |
| `__builtin_arm_*`、Thumb 行內組語（`"rbit %0, %1"`） | 不能組譯 | 第 3、4 節的 C 寫法或 `rv_tools.h` |
| `rbit`（位元反轉 1 條） | 沒有單一指令 | `brev8` + `rev8`（`rv_bitrev32`） |
| 沒有 popcount 指令（M33 要一串位元技巧） | `cpop` 1 條 | `__builtin_popcount` |
| SysTick（`hardware/structs/systick.h`）、SCB、NVIC 暫存器 | Arm 專屬（結構定義在 `structs/systick.h`、`scb.h`、`m33.h`） | `mcycle`、`time_us_64()`；中斷一律用 SDK `irq_*` API |
| `__wfe()` / `__sev()` | SDK 換成 `h3.block` / `h3.unblock` | 照用 SDK 函式即可 |
| 關中斷 `save_and_disable_interrupts()` | SDK 在 RISC-V 改清 `mstatus.MIE`（第 3 位） | 照用 SDK 函式即可 |
| 中斷優先權 `irq_set_priority` | Xh3irq 的優先權行為和 NVIC 是否完全相同 **未查證** | 避免依賴優先權 |
| `sdiv`/`udiv`（M33 有硬體除法） | `div`/`divu` 約 18 週期 | 同第 6 節第 4 條 |

**不受架構影響的**：DMA、PIO、`multicore_*`、`spin_lock_*`、`critical_section_*`、`time_us_64()`、`__not_in_flash_func`，名稱與用法完全一樣。SDK 的 spin lock 在 RISC-V 用 `amoor`/`amoadd` 實作（見 [cheatsheet.md](cheatsheet.md)）。

---

## 8. 記憶體：flash 與 SRAM

| 區域 | 位址 | 大小 | 說明 |
|---|---|---|---|
| XIP（flash，經過快取） | `0x10000000` 起 | 依板子（Pico 2 = 4 MB） | 程式預設放這裡；第一次執行會等 flash |
| XIP 快取 | 當 SRAM 用時在 `0x13FFC000–0x14000000` | **16 KB** | 熱程式碼超過 16 KB 就會一直 miss |
| XIP 不快取別名 | `0x14000000` 起 | — | 讀 flash 但不經過快取 |
| SRAM0–7（交錯） | `0x20000000–0x2007FFFF` | 512 KB | 位址交錯分到 8 個 bank，兩核與 DMA 同時存取較少互搶 |
| SCRATCH_X（SRAM8） | `0x20080000` | 4 KB | `__scratch_x("名稱")` 放變數 |
| SCRATCH_Y（SRAM9） | `0x20081000` | 4 KB | `__scratch_y("名稱")` |

（位址與大小來自 SDK `addressmap.h`；堆疊：core0 的堆疊在 SCRATCH_Y 頂端、core1（`multicore_launch_core1`）在 SCRATCH_X 頂端，各預設 2 KB（`PICO_STACK_SIZE`、`PICO_CORE1_STACK_SIZE` = 0x800，SDK 連結腳本 `section_end.incl`）。所以 `__scratch_x`/`__scratch_y` 放的變數和堆疊共用同一塊 4 KB，放超過約 2 KB 會撞到堆疊。）

**規則**：
- 熱函式：`__not_in_flash_func(名稱)` 或 `__time_critical_func(名稱)` → 放 SRAM，不受 flash 與快取影響。
- `const` 表格會留在 flash（`.rodata` 進 flash）。熱表格不要加 `const`，或開機時複製到 SRAM 陣列。
- 整個程式放 RAM：CMake `pico_set_binary_type(app copy_to_ram)`（要確認題目允許改 CMakeLists）。
- 兩顆核心各自的大陣列放不同 bank 的想法不必做：交錯模式已經自動分散。

---

## 9. 中斷（Xh3irq）

- SDK 的 `irq_set_exclusive_handler`、`irq_set_enabled` 在 RISC-V 一樣能用，底層改成 Hazard3 自己的中斷控制器（CSR `h3.meiea`、`h3.meipa`、`h3.meifa`、`h3.meipra`、`h3.meinext`、`h3.meicontext`），**不要直接碰這些 CSR**。
- `hardware/hazard3.h` 有 `hazard3_irqarray_read/write/set/clear` 巨集，一般用不到。
- 中斷處理函式加 `__not_in_flash_func`；裡面不要 `printf`、不要除法、不要 `float`。

---

## 10. 給小模型的精簡事實（已放進 prompts.md 的通用 prompt）

```text
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
```
