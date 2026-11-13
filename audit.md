# 考前筆記檢查報告（audit）

檢查日期：2026-11-13。對照：pico-sdk 2.3.1 原始碼、Hazard3 官方 repo 文件。
範圍：cheatsheet.md、optimization.md、solutions.md、rp2350-riscv.md、methodology.md、prompts.md（共 93 個程式碼區塊）。
沒動：00-START.md、build-and-debug.md、algorithms.md（另外已測過）。

## 1. 查了什麼

| 項目 | 做法 | 結果 |
|---|---|---|
| SDK 名稱是否存在 | 抽出六個檔案裡所有 SDK 風格的名稱（函式、巨集、型別、結構欄位，共 390 個），逐一在 SDK 原始碼與 pioasm 產生的標頭裡找 | 全部找得到。找不到的 35 個都是筆記自己定義的函式（例如 `dma_copy_words`）、ROS 2 名稱或只寫了前綴 |
| SDK 程式能不能編譯 | 每個 C 區塊做成一個 SDK 專案，`PICO_BOARD=pico2`，`-Wall -Wextra` | **Arm（rp2350-arm-s）**：39 個專案中 37 個編譯並連結成功。**RISC-V（rp2350-riscv）**：39 個全部編譯成功（這台機器缺 RISC-V 的 C++ 函式庫，所以只做到編譯，沒有連結） |
| PIO 程式 | 用 SDK 的 pioasm 組譯 8 支 `.program`（含 RP2350 的 `.pio_version 1`、`.fifo putget`、`irq next`、`wait jmppin`） | 全部通過 |
| Linux C 程式 | gcc 13 `-Wall -Wextra -pthread` 編譯並執行 14 段 | 全部通過；有自我比對的都印「一致」，快速 I/O 與位元巨集的輸出手算核對正確 |
| 組合語言 | 兩段 RV32 `.S` 用 rv32imac+Zba/Zbb/Zbs/Zbkb 組譯 | 通過 |
| Python | 3 段 rclpy 範例做語法檢查 | 通過 |
| 硬體說法 | 週期數、分支預測、`mcountinhibit` 重置值、擴充指令清單、`h3.bextm` 編碼、位址表、預設時脈與電壓、浮點函式庫預設、spin lock 預設、DMA 預設設定、sniff 模式值、編譯旗標 | 見下面 |

已核對正確的硬體說法（節錄）：
- `mul`/`mulh*` 1 週期；`div`/`rem` 18–19 週期；`amo*` 每次嘗試 4 週期；`cm.push`/`cm.pop` 1+n 週期；預測錯多 1 週期；跳到沒對齊的 32 位元指令多 1 週期（Hazard3 文件）。
- 分支預測只記「最後一個往回跳且有跳的分支」。
- `mcountinhibit` 重置值 0x5（`mcycle` 與 `minstret` 都停），SDK 開機程式不會清它。
- SDK 的 `.S` 巨集 `h3.bextm` funct3 寫 0x4、C 巨集與 Hazard3 文件是 0：筆記的提醒正確。
- `pico_double` 在 RISC-V 預設是 compiler 版、`pico_float` 用 `float_single_hazard3.S`。
- `SYS_CLK_HZ` 150 MHz、`VREG_VOLTAGE_DEFAULT` 1.10 V、`VREG_VOLTAGE_MAX` 1.30 V、`PICO_CLOCK_ADJUST_PERI_CLOCK_WITH_SYS_CLOCK` 預設 0。
- 位址表（XIP、XIP SRAM 16 KB、SRAM0–9）、`XIP_CACHE_SIZE` 16 KB、匯流排優先權位元（0/4/8/12）。
- `PICO_USE_SW_SPIN_LOCKS` 在 RP2350 預設開（勘誤 E2）。
- `dma_channel_get_default_config` 的預設值、sniff 模式 0x0/0x1/0x2/0x3/0xe/0xf。
- 不指定 `CMAKE_BUILD_TYPE` 時 SDK 用 Release：`-g -O3 -DNDEBUG`。

## 2. 改了什麼

| 檔案 | 問題 | 改法 |
|---|---|---|
| cheatsheet.md 5.3 插值器範本 | `#include "hardware/hazard3.h"` 沒包條件，**Arm 版編譯失敗**（找不到標頭） | 改成包在 `#if defined(__riscv)` 裡；兩種架構都重新編譯通過 |
| cheatsheet.md 5.2 計時表 | 寫「先 `csrci 0x320, 1`」，但這只開 `mcycle`，`minstret` 仍停 | 改成 `csrci 0x320, 5`，並註明 bit0/bit2 |
| rp2350-riscv.md 第 1 節 | 「約 60 個 RP2350 板子標頭」 | SDK 2.3.1 實際是 78 個 |
| rp2350-riscv.md 位址表 | 「SCRATCH_X/Y 被哪顆核心當堆疊 未查證」 | 已查到：core0 堆疊在 SCRATCH_Y 頂端、core1 在 SCRATCH_X 頂端，各預設 2 KB；放進 scratch 的資料別超過約 2 KB |
| optimization.md 2.2 放置表 | `__scratch_x/y` 只寫「4 KB」，沒提堆疊共用 | 補上同一句提醒 |

## 3. 仍有疑問（沒改）

- **ROS 2（rclcpp）三段 C++**：這台機器沒有 ROS 2，沒編譯。rclpy 只做了語法檢查。
- **RISC-V 只編譯沒連結**：缺 C++ 執行函式庫；同一批程式在 Arm 已完整連結，名稱與簽名沒問題。
- **Hazard3 週期數的前提**：文件假設 `MULDIV_UNROLL=2`、`MUL_FAST=1`。RP2350 實際設定要看 datasheet（這次沒取得），筆記已標未查證。
- **Zbc（clmul）**：筆記說「Hazard3 文件列了、SDK 沒開，不要用」，結論對。RP2350 硬體本身應該也沒實作，但沒拿到 datasheet 確認。
- **「SDK 2.1.0 / 2.2.0 起才有」這類版本說法**：SDK 只抓了 2.3.1 單一版本，沒核對歷史。考場 SDK 版本不同時，以編譯結果為準。
- **工具鏈**：GCC 13（例如 Debian 的 riscv64-unknown-elf-gcc）不認得 `-mcpu=hazard3-rp2350` 與 Zcmp，SDK 會退回第 3 組 `-march`（沒有 Zcb/Zcmp），程式照樣能跑、只是稍大。考場若用官方工具鏈不受影響。
- 「RISC-V 沒有硬體除法器」（prompts.md、optimization.md 的提示）意思是沒有 SIO 除法器；`div` 指令本身有，約 18 週期。字面上有點容易誤解，但後面都寫了週期數，沒改。
- 最高額定時脈：仍未查證（只在 datasheet，下載被擋）。

## 4. 第二輪查證（11-13 晚上，對 pico-sdk 2.3.1 與 pico-examples 原始碼）

- `sleep_ms`/`time_us_64` 不受 `clk_sys` 影響：**已確認**（tick 由 `clk_ref` = 12 MHz XOSC 產生）。
- `set_sys_clock_khz`：不會自己調電壓；`clk_peri` 會改接 48 MHz USB PLL；PIO/PWM 吃 `clk_sys`，超頻後分頻要重算。已寫進 optimization.md 第 6 節、solutions.md。
- DMA sniffer 標準 CRC-32：pico-examples `sniff_crc` 證明 `CRC32R` + `output_reverse` = 沒有最後取反的反射 CRC-32，加 `output_invert` 即標準值（推導，未上板）。
- `irq_set_priority` 在 RISC-V：語意同 Arm，只取高 4 位（16 級），會巢狀搶佔。已寫進 rp2350-riscv.md。
- **修正錯誤**：optimization.md 原寫「XIP 快取停用時可當 RAM」是 RP2040 行為；RP2350 沒有 cache-as-SRAM 視窗，只能釘住快取行。
- `std::hardware_destructive_interference_size`：g++ 13 可、clang 18 不行；`objdump --disassemble=` 在 binutils 2.42 可用。
- 仍未查證（datasheet 兩個網址都被網路代理擋）：PIO `pull noblock` / `mov` 移位計數細節、Hazard3 在 RP2350 的乘除法週期、M33 `udiv` 週期、最高額定時脈、Zbc。
