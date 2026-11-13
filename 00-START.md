# 00-START：考試當天從這裡開始

> 2026-11-14（民國 115 年）成大資工特殊選才上機考。考點：讀文件掌握軟硬體架構、優化程式與整體系統效能；Online Judge 計分，**不能同時提交多次**；只能用考場提供的大語言模型；只能用考場電腦已安裝的軟體（不能自己裝 picotool 等工具）。以上依考試公告。
> 晶片 RP2350，很可能跑 RISC-V（Hazard3）模式。

## 檔案地圖

| 檔案 | 一句話 | 什麼時候開 |
|---|---|---|
| **00-START.md**（這份） | 流程、出事怎麼辦、每段貼給小模型要多少 token | 一開始、卡住時 |
| [templates/](templates/README.md) | **可直接複製的專案骨架**：對拍 + 計時的 `main.c`、定點數、雙核、DMA、PIO 串流、佇列、pthread（都測過） | 寫第一行程式之前 |
| [solutions.md](solutions.md) | 18 種預測題型的完整優化解答；開頭有「題目長這樣 → 跳到哪」 | 看完題目第一個開 |
| [prompts.md](prompts.md) | 貼給小模型的 prompt：A 通用、B1–B17 針對性、C–H 除錯與修正 | 要叫小模型做事時 |
| [algorithms.md](algorithms.md) | 數論、圖論、資料結構、DP、字串範本；開頭有對照表 | 題目像數學或圖論時 |
| [methodology.md](methodology.md) | **0.5 節決策程序**：照順序答 10 個是非題，每個「是」對應一個範本；後面是量測、瓶頸判斷、技巧排名 | 不知道下一步做什麼時 |
| [build-and-debug.md](build-and-debug.md) | CMake 範本、建置、燒錄、`bench.h` 計時對拍、當機診斷 | 編譯、燒錄、當機時 |
| [rp2350-riscv.md](rp2350-riscv.md) | Hazard3 冷門知識：架構判斷、可用指令、行內組語、從 Arm 搬過來的坑 | 位元運算、組語、浮點、計週期 |
| [cheatsheet.md](cheatsheet.md) | 所有查證過的 API 名稱與用法（DMA、PIO、多核、鎖、ROS 2） | 不確定某個函式存不存在時 |
| [optimization.md](optimization.md) | 各種優化技巧的細節、前後對照、實測數字 | 想知道某招為什麼有效時 |

## 開考後第一個 10 分鐘：環境偵察（每項都記在紙上）

先弄清楚環境，後面才不會白做。指令以 Linux 為準，Windows 就看檔案總管與題目說明。

| 要知道的 | 怎麼看 | 影響什麼 |
|---|---|---|
| 架構：RISC-V 還是 Arm | `grep -e PICO_PLATFORM: -e PICO_BOARD: -e CMAKE_C_COMPILER: build/CMakeCache.txt`（`rp2350-riscv` 或 `rp2350-arm-s`）；或看編譯器名稱：`riscv32-*-elf-gcc` 是 RISC-V，`arm-none-eabi-gcc` 是 Arm | Arm 版：prompt A 刪掉【RP2350 RISC-V 事實】那段；`rp2350-riscv.md` 只在 RISC-V 用 |
| SDK 版本 | `grep 'set(PICO_SDK_VERSION_' $PICO_SDK_PATH/pico_sdk_version.cmake` | 2.2.0 以前沒有 `dma_channel_config_t`、`dma_encode_transfer_count`（cheatsheet 有標） |
| 有哪些工具 | `which picotool cmake ninja make riscv32-unknown-elf-gcc arm-none-eabi-gcc` | 沒有 picotool 就用拖放 `.uf2` 燒錄 |
| 怎麼提交、看什麼分數 | 題目說明 | 決定先求對還是先求快 |
| 小模型能力 | 貼 A，問「RP2350 的 DMA count 單位是什麼？」，要答「傳輸筆數」；再問「A 裡最後一條事實是什麼？」答得出來才代表 A 沒被截斷 | 答不好就改用 A-mini，題目只貼相關函式 |

**第一件實作的事：原版程式不改，先建置、跑一次、記下輸出和時間。** 這是之後每一版對拍與比速度的基準。

**每一版都留檔**：`cp main.c v1_ok.c`、`v2_dma.c`……檔名寫「有沒有通過」。評測不能同時提交多次，出錯時可以馬上交回上一個正確版。

**板子出狀況時**：純計算的部分可以先在電腦上用一般 `gcc -O2` 測正確性（[build-and-debug.md](build-and-debug.md) 的 `bench.h` 在電腦上也能用），等板子好了再上板量。

## 考試當天流程

| 時間 | 做什麼 |
|---|---|
| 09:00–09:10 | 聽說明。記下：怎麼提交（GitLab？上傳？）、評分看什麼（時間？正確率？資源？）、能不能改 CMakeLists |
| 09:10–09:40 | **每題**做三件事：①用 [solutions.md](solutions.md) 速查表和 [algorithms.md](algorithms.md) 第 1 節對照表分類 ②寫下輸入範圍、時間限制、題目給的 API ③給每題估「能拿多少分、要多久」，先做划算的 |
| 09:40–12:00 | 每題先交一個**正確**的版本拿基本分。用 prompt：A → 對應的 B（或 B0 + 完整解答）。每次提交前先在本機比對原版輸出 |
| 12:30 起 | 教室重新開放，可以繼續 |
| 13:00–16:00 | 優化循環：量 → 照 [methodology.md](methodology.md) 選下一層 → 改一件事 → 比對 → 量 → 有變快才交 |
| 16:00–16:30 | 不再大改；確認每題最後一次提交是正確而且最快的那版 |
| 16:30 之後 | 可能延長到 17:30 或 19:30，看現場宣布 |

### 一題的標準動作（每題重複）

1. **讀**：題目 + 給的文件。文件段落看不懂 → [prompts.md](prompts.md) H3 轉成表。給的程式看不懂 → H2 五行說明。
2. **分類**：[methodology.md](methodology.md) 0.5 節的是非題，或 [solutions.md](solutions.md) 速查表找 S 編號；數學或圖論 → [algorithms.md](algorithms.md) 第 1 節。
   **先拿正確**：複製 [templates/](templates/README.md)，原版貼進 `work_ref`，看到 `ALL PASS` 就存 `v0_ok.c` 交一次。
3. **開新對話**，貼 A（通用 prompt）。
4. **貼 B**：對應的 B 編號（見下面預算表），填好題目與程式。沒有對應的 → B0 + 完整解答那一節。
5. **建置**：照 [build-and-debug.md](build-and-debug.md) 第 2 節。編譯錯誤 → C 或 H1。
6. **比對**：`bench_same_u32` 或 F 對拍，和原版完全一樣才往下。
7. **量**：`BENCH`，記下數字。
8. **提交**：一次一個，等結果回來才交下一個。
9. **還有時間** → D 找瓶頸，回到第 4 步疊下一層。
10. **停損**：同一招 30 分鐘還沒通過對拍 → 退回上一個 `vN_ok.c`，換下一招或先做別題。DMA / PIO / 雙核這類硬體招最容易卡，演算法和迴圈內的招先做。

## 出事了怎麼辦

| 狀況 | 做法 |
|---|---|
| 編譯失敗 | 看**第一個**錯誤。缺標頭或 `undefined reference` → [build-and-debug.md](build-and-debug.md) 第 1 節的連結庫表；函式名稱可疑 → 查 [cheatsheet.md](cheatsheet.md)；再不行貼 [prompts.md](prompts.md) H1 |
| 燒不進板子 | 按住 BOOTSEL 再插 USB，看有沒有出現 USB 磁碟；換線、換孔；有 picotool 才用 `picotool load -f -x app.uf2`；Online Judge 自動燒錄的話不用管這個 |
| 程式沒輸出 / 突然停住 | RISC-V 例外會讓核心靜靜睡著。裝 [build-and-debug.md](build-and-debug.md) 6.1 的 `fault_report_install()`；USB 輸出加等待連線 |
| 當機（mcause 4 / 6） | 沒對齊的讀寫 → `memcpy`；詳見 [build-and-debug.md](build-and-debug.md) 6.1 |
| 卡住不動 | spin lock 裡呼叫了 `printf` / `sleep`；core1 沒 launch；等的是 TX 不是 RX；見 6.2 |
| 評測說答案錯 | 和原版對拍，測邊界（長度 0、1、不是 4 的倍數、最大值、負數）；查溢位；先退回上一個正確版本再交一次保分 |
| 評測說太慢 | [methodology.md](methodology.md) 第 0 節；[prompts.md](prompts.md) D 找瓶頸；先看演算法（[solutions.md](solutions.md) S18） |
| 小模型亂寫、編出不存在的函式 | 開新對話，重貼 A + B；它說的名稱查 [cheatsheet.md](cheatsheet.md)；改用 H1 一次只修一個錯 |
| 小模型開始重複或胡言亂語 | context 滿了。開新對話；只貼相關函式；A 換成 A-mini |
| 小模型解釋得很長但不給程式 | 回它：「只輸出完整函式，不要解釋。」 |
| 完全不知道從哪下手 | [methodology.md](methodology.md) 第 6 節「卡住了？依序試這 20 件事」 |

## 貼上預算表（給 8k context 的小模型）

**估算方式**：中文字（含全形標點）每字算 1 token，其他字元（英文、程式碼）每 3.6 字元算 1 token。這比「中文 3 字 1 token」保守，實際通常更少。**目標**：A + B（+ 參考解答）≤ 5,000 token，留 ≥ 3,000 給你的題目、程式和模型的回答。所有組合都在 4,500 以內（A 已含現成模組清單），不用再拆。

### 固定要貼的

| 段落 | 約 token | 說明 |
|---|---|---|
| A 通用 prompt（含 RISC-V 事實、現成模組清單） | 1,550 | 每個新對話第一則。確定是 Arm 版可以刪掉 RISC-V 事實那段，剩約 900 |
| A-mini | 200 | context 很緊時取代 A |

### 依題型：A + B，必要時再加完整解答 S

| 題型 | 針對性 prompt | B 約 token | 完整解答 | S 約 token | A + B | A + B + S |
|---|---|---|---|---|---|---|
| DMA 取代 memcpy / 複製迴圈 | B1 | 500 | [S01](solutions.md#s01) | 900 | 2100 | 3000 |
| DMA 填值 / memset | B2 | 500 | [S02](solutions.md#s02) | 700 | 2050 | 2800 |
| DMA → PIO → DMA 資料管線 | B3 | 550 | [S03](solutions.md#s03) | 1400 | 2150 | 3550 |
| PIO 變換程式 | B4 | 300 | [S04](solutions.md#s04) | 450 | 1900 | 2350 |
| Checksum / CRC32 | B5 | 600 | [S05](solutions.md#s05) | 1000 | 2150 | 3150 |
| 大陣列運算切給兩顆核心 | B6 | 450 | [S06](solutions.md#s06) | 800 | 2050 | 2850 |
| 兩顆核心共用計數器（板子上的 critical section） | B7 | 400 | [S07](solutions.md#s07) | 1050 | 2000 | 3050 |
| 生產者 / 消費者：無鎖 SPSC 環形佇列 | B8 | 650 | [S08](solutions.md#s08) | 1100 | 2250 | 3300 |
| 位元運算核心（popcount、parity、反轉、欄位） | B9 | 250 | [S09](solutions.md#s09) | 1100 | 1850 | 2950 |
| 查表 vs 現算 | B10 | 200 | [S10](solutions.md#s10) | 750 | 1800 | 2550 |
| 排序與搜尋 | B11 | 250 | [S11](solutions.md#s11) | 1000 | 1800 | 2800 |
| ROS 2 processor：低延遲、高吞吐 | B12 | 450 | [S12](solutions.md#s12) | 1100 | 2050 | 3100 |
| pthread 平行 reduce | B13 | 400 | [S13](solutions.md#s13) | 1000 | 2000 | 3000 |
| 修好練習題的 critical section | B7 | 400 | [S14](solutions.md#s14) | 950 | 2000 | 2900 |
| 矩陣乘法 / 2D 陣列：迴圈順序 | B14 | 300 | [S15](solutions.md#s15) | 550 | 1900 | 2450 |
| 手寫 RISC-V 組合語言 | B15 | 550 | [S16](solutions.md#s16) | 900 | 2150 | 3050 |
| 全層疊加：DMA + PIO + 兩顆核心同時跑（串流處理） | B16 | 600 | [S17](solutions.md#s17) | 2300 | 2150 | 4450 |
| 演算法層實測：先換演算法才是輾壓 | B17 | 350 | [S18](solutions.md#s18) | 2550 | 1950 | 4450 |

- 「A + B」通常就夠了，因為每個 B 已經附了參考解答的關鍵程式。
- S17、S18 加上完整解答會到 4,450：S17 只貼它的程式區塊（約 1,600）；S18 不用貼完整解答，B17 已經有換演算法的對照表，裡面那支量測程式是給你自己在電腦上跑的。
- 題目很長時（> 2,500 token），不要貼 S，只貼 A + B，並且只貼和題目有關的函式。

### 其他 prompt（都短，接在 A 後面）

| 段落 | 約 token | 用途 |
|---|---|---|
| B0 把參考解答改成這題 | 250 | 沒有對應 B 時，B0 + solutions.md 那一節 |
| C 編譯錯誤 | 300 | |
| D 找瓶頸 | 350 | |
| E ROS 2 除錯 | 300 | |
| F 對拍測試 | 200 | |
| G 數論 / 圖論 | 150 | 再加 algorithms.md 的一小節 |
| H1 修正迴圈 | 350 | 每次失敗重貼 |
| H2 五行看懂程式 | 150 | |
| H3 文件 → 表格 | 300 | 再加文件段落 |

### algorithms.md（只貼「小節」，不要貼整章）

| 章 | 整章約 token | 說明 |
|---|---|---|
| 1. 對照表 | 2,250 | 自己看就好，找到要用的小節編號 |
| 3. 數論範本 | 7,800 | 太大，**只貼一個小節**（每節 150–1,000），注意標題寫的「需要 3.x」也要一起貼 |
| 5. 圖論範本 | 6,300 | 同上，每節 150–700 |
| 6. 區間查詢與資料結構 | 2,700 | 每節 200–500 |
| 7. DP 與搜尋 | 1,700 | |
| 8. 字串與卷積 | 1,450 | |
| 10.1 分類 prompt | 900 | 數學題先跑這個分類 |

最大的小節是 3.17（64 位元質數判斷，約 1,000，還需要 3.7、3.10、3.16 的函式）。A + G + 一個小節 + 它需要的小節，最多約 3,500。

### templates/（只貼要改的函式或要用的那個 .h）

| 段落 | 約 token |
|---|---|
| `main.c` 裡的 `work_ref` + `work_fast` 兩個函式（範例大小） | 250 |
| `fixed.h` | 1,570 |
| `dual_core.h` / `dma_util.h` / `pio_stream.h` / `spsc_ring.h` | 590 / 680 / 580 / 510 |
| `linux-app/par.h` | 620 |

### rp2350-riscv.md 與 build-and-debug.md

| 段落 | 約 token |
|---|---|
| RISC-V 精簡事實（已在 A 裡，不用另外貼） | 600 |
| rp2350-riscv.md 第 4 節 `rv_tools.h` | 1,150 |
| rp2350-riscv.md 第 3 節 指令表 | 600 |
| rp2350-riscv.md 第 7 節 從 Arm 搬過來的坑 | 550 |
| build-and-debug.md 第 5 節 `bench.h` + 範例 | 1,200 |
| build-and-debug.md 6.1 當機回報 | 800 |

## 開考前五分鐘檢查

- [ ] 這套檔案都打得開，連結會跳
- [ ] 知道考場小模型的對話框在哪、能不能設 system prompt（能的話 A 放那裡）
- [ ] 知道怎麼提交、提交後去哪看結果
- [ ] 上面「環境偵察」表格的五項都記下來了
