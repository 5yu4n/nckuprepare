# nckuprepare

成大資工特殊選才上機考（2026-11-14）考前筆記。考場板子是 **RP2350B 機器人控制板**（6 顆 RGB LED、蜂鳴器、2 顆按鍵、4 個編碼器馬達、伺服排針、I2C×2、UART0、SPI LCD、無線模組），pico-sdk，Arm 或 RISC-V（Hazard3）都可能。考場只能用現場的小模型（約 8k context），所以筆記大多寫成「可以直接貼給小模型」的段落。

> **先開 [board.md](board.md)**：板上每個周邊怎麼用、腳位怎麼找、RP2350B 的坑。現成專案在 [templates/board-app/](templates/board-app/)（改 `board_pins.h` 的腳位就能編）。

## 考試當天：照這個順序開

0. **[board.md](board.md)**：板子周邊與腳位。題目要動 LED、馬達、蜂鳴器、按鍵、LCD、感測器時先看這份，程式從 [templates/board-app/](templates/board-app/) 抄。
1. **[00-START.md](00-START.md)**：開考後第一個 10 分鐘的環境偵察（架構、SDK 版本、工具、小模型測試），再來是流程、時間表、出事怎麼辦、每段 prompt 多少 token。
2. 寫程式前 → 複製 **[templates/](templates/README.md)**（對拍 + 計時骨架與常用模組，都測過），原版貼進 `work_ref`。
3. 看完題目 → **[methodology.md](methodology.md) 0.5 節**的是非題選招，或 **[solutions.md](solutions.md)** 開頭的「題目長這樣 → 跳到哪」找 S 編號。
4. 叫小模型做事 → **[prompts.md](prompts.md)**：先貼 A（通用），再貼對應的 B。
5. 編譯 / 燒錄 / 當機 → **[build-and-debug.md](build-and-debug.md)**。

## 30 秒急救

| 狀況 | 開哪裡 |
|---|---|
| 不知道題目屬於哪一類 | [solutions.md](solutions.md) 速查表；數學、圖論 → [algorithms.md](algorithms.md) 第 1 節 |
| 題目要動板上的 LED / 馬達 / 蜂鳴器 / 按鍵 / LCD / 感測器 | [board.md](board.md) + [prompts.md](prompts.md) B18 |
| GPIO 30 以上沒反應 | 用了 `PICO_BOARD=pico2`；改用 board-app 的 `exam_rp2350b` → [board.md](board.md) 第 0 節 |
| 編譯失敗 | 看第一個錯誤 → [build-and-debug.md](build-and-debug.md) 第 1 節；函式名稱可疑 → [cheatsheet.md](cheatsheet.md) |
| 程式沒輸出、突然停 | RISC-V 例外會讓核心睡著 → [build-and-debug.md](build-and-debug.md) 6.1 |
| 太慢 | [methodology.md](methodology.md) 第 0 節 → [prompts.md](prompts.md) D |
| 小模型亂掰函式 | 開新對話重貼 A + B，名稱查 [cheatsheet.md](cheatsheet.md) |
| 不知道下一招 | [methodology.md](methodology.md) 0.5 節決策程序 |
| 完全卡住 | [methodology.md](methodology.md) 第 6 節「依序試這 20 件事」 |

## 檔案地圖

| 檔案 | 內容 |
|---|---|
| [board.md](board.md) | **考場板子**：RP2350B 周邊對照、找腳位、每個周邊的 SDK 寫法與常見坑 |
| [templates/board-app/](templates/board-app/) | 板子現成專案：`board_pins.h`（腳位）、`board.h`（LED、蜂鳴器、按鍵、馬達、編碼器、伺服、I2C、LCD、UART、ADC）、掃腳模式 |
| [00-START.md](00-START.md) | 考試當天入口：流程、出事怎麼辦、貼上預算表 |
| [templates/](templates/README.md) | 可直接複製的專案骨架與模組（pico-sdk 與 Linux），附測試 |
| [solutions.md](solutions.md) | 17 種預測題型（S01–S18，S12 ROS 2 已刪）的完整優化解答 |
| [prompts.md](prompts.md) | 貼給小模型的 prompt：A 通用、A-mini、B0–B17 針對性、C–H 除錯與修正 |
| [algorithms.md](algorithms.md) | 數論、圖論、資料結構、DP、字串範本，含在 RV32 上的加速倍數 |
| [methodology.md](methodology.md) | 優化方法論：量 → 判斷瓶頸 → 由上往下試 → 比對 → 再量 |
| [build-and-debug.md](build-and-debug.md) | CMake 範本、建置、燒錄、`bench.h` 計時對拍、當機診斷 |
| [rp2350-riscv.md](rp2350-riscv.md) | Hazard3 冷門知識：判斷架構、可用指令、行內組語、從 Arm 搬過來的坑 |
| [cheatsheet.md](cheatsheet.md) | 查證過的 API 名稱與用法（DMA、PIO、多核、鎖） |
| [optimization.md](optimization.md) | 各種優化技巧的細節、前後對照、實測數字 |
| [audit.md](audit.md) | 程式碼查核紀錄：哪些編譯過、修了什麼、哪些還沒驗證 |
| [algorithms-tests/](algorithms-tests/) | algorithms.md 範本的原始碼與對拍測試（`gcc -O2 test_nt.c && ./a.out`） |

## 可信度

- pico-sdk 的函式名稱對過 SDK 2.3.1 原始碼；用到 2.2.0 以後才有的名稱會標註。
- C 範本用 RISC-V（rv32 Hazard3 的 `-march`）與 Cortex-M33 編譯過；`.pio` 用 pioasm 組譯過。
- **沒有上板實測**。datasheet 才有的數字（最高時脈、除法週期數等）未逐一驗證，各檔會標「未查證」。
