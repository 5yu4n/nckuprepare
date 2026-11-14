# templates：考場直接複製的專案骨架

考場不要從零寫。複製整個資料夾，只改 `main.c` 裡標「改這裡」的地方。

## 怎麼用

| 題目在哪跑 | 複製這個 | 第一步 |
|---|---|---|
| **考場板子周邊**（LED、蜂鳴器、按鍵、馬達、編碼器、伺服、I2C、LCD、UART、ADC） | [board-app/](board-app/) | 改 `board_pins.h` 腳位；`cmake -DMODE=1 ..` 掃輸入腳、`-DMODE=2` 掃 LED 腳。說明在 [../board.md](../board.md) |
| RP2350 板子（pico-sdk）優化題對拍 | [pico-app/](pico-app/) | 從 `$PICO_SDK_PATH/external/` 複製 `pico_sdk_import.cmake` 進來，照 `CMakeLists.txt` 開頭建置 |
| Linux（電腦上對拍、pthread） | [linux-app/](linux-app/) | `make && ./app` |
| 板子壞了或還沒拿到 | [pico-app/](pico-app/) | `gcc -O2 main.c -o t && ./t`：同一份 `main.c` 在電腦上也能編，先驗證邏輯 |

`main.c` 的流程：原版 `work_ref` 當正確答案 → 7 種測資（亂數、全 0、全 1、遞增、遞減……）× 16 種長度（0、1、不是 4 的倍數、剛好跨過門檻、最大）逐一對拍 → 全部一樣才計時 → 印出 `ALL PASS`。有錯就印 `FAIL` 和第一個不同的位置。

## 模組（都是單一標頭，`#include` 就能用）

| 檔案 | 給什麼 | 貼給小模型約 token |
|---|---|---|
| [pico-app/main.c](pico-app/main.c) | 對拍 + 計時骨架（板子與電腦都能編） | 1,570 |
| [pico-app/bench.h](pico-app/bench.h) | `BENCH` 計時取最快、`bench_same_u32` 對拍、`bench_rand` 固定亂數 | 680 |
| [pico-app/fixed.h](pico-app/fixed.h) | 定點數 Q16.16 / Q1.15、飽和運算、整數平方根、sin/cos 查表（RISC-V 沒有 FPU 時取代 float） | 1,570 |
| [pico-app/dual_core.h](pico-app/dual_core.h) | `dual_init()` + `dual_run(work, ctx, n)`：兩顆核心各做一半 | 590 |
| [pico-app/dma_util.h](pico-app/dma_util.h) | `dma_copy`、`dma_copy_async` / `dma_wait`、`dma_fill32` | 680 |
| [pico-app/pio_stream.h](pico-app/pio_stream.h) | DMA → PIO → DMA 串流：`pio_stream_init` / `start` / `wait`（S03 模組版） | 580 |
| [pico-app/xform.pio](pico-app/xform.pio) | 三支 PIO 變換程式（反相、位元反轉、原樣）+ `xform_init`；CMake 打開 `pico_generate_pio_header` 那行 | 320 |
| [pico-app/spsc_ring.h](pico-app/spsc_ring.h) | 無鎖單生產者單消費者佇列（兩核或兩執行緒之間傳資料） | 510 |
| [pico-app/rv_tools.h](pico-app/rv_tools.h) | Hazard3 指令包裝（`brev8`、`orc.b`、`zip`）、`mcycle` 週期計數；非 RISC-V 自動用 C | 850 |
| [pico-app/fault_report.h](pico-app/fault_report.h) | 當機時印出 `mcause` 和 `pc`（RISC-V 預設會靜靜睡著） | 420 |
| [pico-app/CMakeLists.txt](pico-app/CMakeLists.txt) | 連好上面模組要的庫 | 400 |
| [linux-app/main.c](linux-app/main.c) | Linux 版對拍 + 計時 + 平行版骨架 | 1,010 |
| [linux-app/par.h](linux-app/par.h) | `par_for(n, work, ctx)`：切給所有核心；`par_slot_t` 每執行緒一條快取行 | 620 |
| [linux-app/Makefile](linux-app/Makefile) | `make`（-O3）、`make debug`（ASan + UBSan）、`make tsan`（資料競爭） | 160 |

叫小模型改程式時：貼 prompts.md 的 A，再貼「`main.c` 的 work_ref 和 work_fast 兩個函式」就好，不要貼整個檔案。要它用某個模組時，只貼那個 `.h`。

## 已經驗證的

- 電腦（gcc 13）：`sh tests/run_all.sh` 全過。`fixed.h` 對 libm 比對：`isqrt32` 400 萬個值全對；`q16_mul_round` 誤差 ≤ 0.5 LSB；`q16_sqrt` ≤ 1 LSB；sin/cos 最大 1.42 LSB（Q1.15）。`spsc_ring.h` 與 `par.h` 在 ThreadSanitizer 下沒有警告。
- 板子：`pico-app/main.c` 和所有標頭（`pio_stream.h` 用手寫的 PIO 程式結構代替 pioasm 產物），用 pico-sdk 2.3.1 的標頭，以 RISC-V（riscv64-unknown-elf-gcc 13，`-march=rv32imac_zicsr_zifencei_zba_zbb_zbs_zbkb`）和 Arm（arm-none-eabi-gcc 13，Cortex-M33）編譯通過，`-Wall -Wextra` 零警告。USB 和 UART 兩種 stdio 設定都試過。
- **沒做**：沒有連結成 `.uf2`、沒有上板跑（這台機器不能建 pico-sdk 的工具）。上板第一次先跑原樣的範例，看到 `ALL PASS` 再改。

## 注意

- 三個 `uint32_t` 陣列 × `N` × 4 bytes 要放得進 520 KB SRAM，預設 `N = 16384`（192 KB）。
- `dual_core.h` 的 `dual_init()` 會啟動 core1，**只能呼叫一次**；程式其他地方不要再 `multicore_launch_core1`。
- `fixed.h` 的 `q16_div` 用 64 位元除法（軟體函式，很慢），只在迴圈外用；迴圈裡除以同一個數 → 先算倒數再乘（algorithms.md 的 fastdiv）。
