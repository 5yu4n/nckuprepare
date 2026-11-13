# nckuprepare

成大資工特殊選才上機考（2026-11-14）考前筆記。晶片 RP2350（很可能是 RISC-V Hazard3 模式）、pico-sdk、PIO / DMA / 多核、可能有 ROS 2。考場只能用現場的小模型（約 8k context），所以筆記大多寫成「可以直接貼給小模型」的段落。

## 考試當天：照這個順序開

1. **[00-START.md](00-START.md)**：流程、時間表、出事怎麼辦、每段 prompt 多少 token。
2. 看完題目 → **[solutions.md](solutions.md)** 開頭的「題目長這樣 → 跳到哪」，找 S 編號。
3. 叫小模型做事 → **[prompts.md](prompts.md)**：先貼 A（通用），再貼對應的 B。
4. 編譯 / 燒錄 / 當機 → **[build-and-debug.md](build-and-debug.md)**。

## 30 秒急救

| 狀況 | 開哪裡 |
|---|---|
| 不知道題目屬於哪一類 | [solutions.md](solutions.md) 速查表；數學、圖論 → [algorithms.md](algorithms.md) 第 1 節 |
| 編譯失敗 | 看第一個錯誤 → [build-and-debug.md](build-and-debug.md) 第 1 節；函式名稱可疑 → [cheatsheet.md](cheatsheet.md) |
| 程式沒輸出、突然停 | RISC-V 例外會讓核心睡著 → [build-and-debug.md](build-and-debug.md) 6.1 |
| 太慢 | [methodology.md](methodology.md) 第 0 節 → [prompts.md](prompts.md) D |
| 小模型亂掰函式 | 開新對話重貼 A + B，名稱查 [cheatsheet.md](cheatsheet.md) |
| 完全卡住 | [methodology.md](methodology.md) 第 6 節「依序試這 20 件事」 |

## 檔案地圖

| 檔案 | 內容 |
|---|---|
| [00-START.md](00-START.md) | 考試當天入口：流程、出事怎麼辦、貼上預算表 |
| [solutions.md](solutions.md) | 18 種預測題型（S01–S18）的完整優化解答 |
| [prompts.md](prompts.md) | 貼給小模型的 prompt：A 通用、A-mini、B0–B17 針對性、C–H 除錯與修正 |
| [algorithms.md](algorithms.md) | 數論、圖論、資料結構、DP、字串範本，含在 RV32 上的加速倍數 |
| [methodology.md](methodology.md) | 優化方法論：量 → 判斷瓶頸 → 由上往下試 → 比對 → 再量 |
| [build-and-debug.md](build-and-debug.md) | CMake 範本、建置、燒錄、`bench.h` 計時對拍、當機診斷 |
| [rp2350-riscv.md](rp2350-riscv.md) | Hazard3 冷門知識：判斷架構、可用指令、行內組語、從 Arm 搬過來的坑 |
| [cheatsheet.md](cheatsheet.md) | 查證過的 API 名稱與用法（DMA、PIO、多核、鎖、ROS 2） |
| [optimization.md](optimization.md) | 各種優化技巧的細節、前後對照、實測數字 |
| [audit.md](audit.md) | 程式碼查核紀錄：哪些編譯過、修了什麼、哪些還沒驗證 |
| [algorithms-tests/](algorithms-tests/) | algorithms.md 範本的原始碼與對拍測試（`gcc -O2 test_nt.c && ./a.out`） |

## 可信度

- pico-sdk 的函式名稱對過 SDK 2.3.1 原始碼；用到 2.2.0 以後才有的名稱會標註。
- C 範本用 RISC-V（rv32 Hazard3 的 `-march`）與 Cortex-M33 編譯過；`.pio` 用 pioasm 組譯過。
- **沒有上板實測**。datasheet 才有的數字（最高時脈、除法週期數等）和 rclcpp 範例未逐一驗證，各檔會標「未查證」。
