# 優化方法論：照著走就能硬解

> 什麼都不記得、小模型也不可靠的時候，就照這份一步一步做。每一步都只問一個問題，答案決定下一步。
> 相關檔案：[00-START.md](00-START.md)（考試當天入口）、[build-and-debug.md](build-and-debug.md)（計時、對拍、當機診斷）、[solutions.md](solutions.md)（18 種預測題型的完整解答）、[optimization.md](optimization.md)（各種技巧的細節與範例）、[cheatsheet.md](cheatsheet.md)（API 名稱）、[algorithms.md](algorithms.md)（數論、圖論）、[prompts.md](prompts.md)（貼給小模型）。
> 標「實測」的數字是我在 4 核 Linux VM 上量的；沒標的是通則，用之前自己量。

## 0. 一頁版（先看這個）

```
1. 讀懂：輸入範圍多大？時間限制多少？評分看什麼（正確、時間、資源）？
2. 先做出「正確」的版本，存起來當對照組（ref）。
3. 量：整體多久？哪一段最久？（只優化最久的那段）
4. 判斷瓶頸：算太多？等記憶體？等 I/O？等鎖？等硬體？
5. 照第 4 節的表，從「層級最高」的招開始試：演算法 > 資料結構與排列 > 編譯器 > 微架構 > 平行 > 硬體卸載 > 系統設定
6. 一次只改一件事。
7. 跟 ref 比對輸出，必須完全一樣（隨機輸入 + 邊界輸入）。
8. 再量。有變快才留，沒變快就退回。
9. 回到 3，直到夠快或沒時間。
10. 交之前：關掉除錯輸出、確認編譯等級、確認只交一次（OJ 規則不能同時多次提交）。
```

**要輾壓，不要只贏一點**：每一層都疊上去，總加速是相乘的。演算法換掉（常是 100 倍以上）→ 資料循序、零複製 → 熱函式放 RAM、`-O3` → 搬運交給 DMA、轉換交給 PIO → 兩顆核心同時算，而且和 DMA/PIO 重疊。先做結構性重寫（把「搬 → 變換 → 算」改成管線），再摳單一迴圈。每題的疊加配方和實測總加速在 [solutions.md](solutions.md) 的「輾壓原則」；最完整的範本是 S17（DMA + PIO + 雙核）與 S18（演算法層實測：數對計數 338 ms → 0.15 ms）。

---

## 1. 讀題：先算出需要的複雜度

**輸入大小 → 能接受的複雜度**（一般 PC 大約每秒 10⁸ 個簡單運算；RP2350 150 MHz 單核最多約每秒 1.5×10⁸ 條指令，實際要打折）：

| n 上限 | 可接受 | 典型做法 |
|---|---|---|
| ≤ 10 | O(n!) | 全排列暴力 |
| ≤ 20 | O(2ⁿ · n) | 位元枚舉子集、狀態壓縮 DP |
| ≤ 500 | O(n³) | 三層迴圈、Floyd-Warshall |
| ≤ 5,000 | O(n²) | 兩層迴圈、簡單 DP |
| ≤ 10⁶ | O(n log n) | 排序、二分、堆積、分治 |
| ≤ 10⁸ | O(n) | 一次掃描、雙指標、前綴和 |
| > 10⁸ 或 10¹⁸ | O(log n) 或 O(1) | 公式、快速冪、矩陣快速冪、數學推導 |

**題目「聞起來」像數學或圖論**（計數、取模 10⁹+7、質數、gcd/lcm、因數、組合數、路徑、最短路、連通、樹、依賴順序、DP）→ 先查 [algorithms.md](algorithms.md)，通常有直接的 O(n log n) 或公式解，比微優化有效幾百倍。

**評分方式要問清楚**：是看總時間、最慢的一筆、還是 CPU 使用率？是否允許超頻、改時脈、用第二顆核心？讀題目文件（考試要點第 1 條就是「透過文件閱讀掌握軟硬體架構」），題目給的 API 永遠優先於 SDK。

---

## 2. 量測：不量不改

| 環境 | 工具 |
|---|---|
| RP2350 | `time_us_64()`；細到週期：RISC-V `mcycle`（先 `csrci mcountinhibit, 5`）、Arm `m33_hw->dwt_cyccnt`（範本見 [optimization.md](optimization.md) 第 1 節） |
| Linux 整支程式 | `time ./app < in.txt`（看 real 與 user：user ≫ real 代表平行有效；real ≫ user 代表在等 I/O 或鎖） |
| Linux 函式層級 | 手動 `clock_gettime(CLOCK_MONOTONIC)`；有裝的話 `perf stat ./app`、`perf record` + `perf report`；`gcc -pg` + `gprof`（環境有沒有裝：未查證，考場先試） |
| ROS 2 | `ros2 topic hz`、`ros2 topic bw`、`ros2 topic delay`（訊息要有 header） |

**量測規則**：
- 同一段跑多次（例如 20 次）取**最小值**；第一次常包含快取冷啟動、分頁錯誤。
- 輸入要夠大，讓時間至少幾毫秒，否則量到的是雜訊。
- 熱迴圈裡不要 `printf`；量的時候也不要開除錯輸出。
- 回報：`原版 X → 改後 Y，加速 X/Y 倍（條件：時脈、-O 等級、取幾次最小值）`。

**Amdahl 定律**：某段占總時間 p，把它加速 s 倍，整體加速 = 1 / ((1−p) + p/s)。占 10% 的部分就算變無限快，整體最多快 1.11 倍 → **只優化最久的那段**。

---

## 3. 判斷瓶頸類型

| 類型 | 症狀 | 怎麼確認 | 主要對策 |
|---|---|---|---|
| **算太多（compute）** | CPU 100%、時間跟運算次數成正比 | 換更小的資料，時間等比例下降 | 換演算法 → 減少運算（查表、強度削減）→ 平行 |
| **等記憶體（memory）** | 資料大、存取跳來跳去 | 同樣運算量，循序存取比隨機存取快很多 | 改資料排列、迴圈順序、分塊；MCU 上把程式與資料放 SRAM |
| **等 I/O** | real ≫ user；`printf`/`scanf`/USB/UART 多 | 把輸出關掉時間大減 | 批次讀寫（fread/fwrite）、減少輸出、熱迴圈不印 |
| **等同步（sync）** | 多執行緒反而沒變快或變慢 | 執行緒從 1 加到 4，時間沒降 | 縮短臨界區、各自累加最後合併、atomic、無鎖佇列 |
| **等硬體（peripheral）** | CPU 在 `while(busy)` 或 `*_blocking` 裡等 | 看程式在哪個迴圈空轉 | DMA、PIO、IRQ，讓等待和運算重疊 |

---

## 4. 技巧排名表（由上往下試）

### 層級 1：演算法與複雜度（收益：數十倍到無限）

| 情況 | 換成 |
|---|---|
| 兩層迴圈找配對 / 查有沒有 | 排序 + 二分，或雜湊表 → O(n log n) / O(n) |
| 重複算區間和 | 前綴和（O(1) 查詢）；2D 也有前綴和 |
| 區間內最大/最小，視窗滑動 | 單調佇列 O(n) |
| 已排序陣列找兩數和 | 雙指標 O(n) |
| 重複子問題 | 記憶化 / DP |
| 要第 k 小 | quickselect 平均 O(n)（[solutions.md](solutions.md) S11） |
| 重複算 aⁿ、費氏數列第 n 項 | 快速冪、矩陣快速冪 O(log n) |
| 整數範圍小的排序 | 計數排序 / 基數排序 O(n)（實測 1M 個 byte：qsort 121 ms → 0.8 ms） |
| 圖的最短路、連通、依賴 | BFS、Dijkstra、並查集、拓撲排序（[algorithms.md](algorithms.md)） |
| 質數、因數、gcd、取模組合數 | 篩法、歐幾里得、模反元素（[algorithms.md](algorithms.md)） |
| 答案有單調性（越大越可行） | 二分答案 |

### 層級 2：資料結構與排列（收益：2–10 倍）

- 最內層迴圈循序存取：矩陣乘法 i-j-k → i-k-j（實測 115 ms → 23 ms）。
- 陣列的結構（SoA）優於結構的陣列（AoS），當只用到部分欄位時。
- 結構欄位大的放前面減少 padding；常一起用的放一起。
- 用陣列代替鏈結串列；預先配置，不在迴圈裡 `malloc`。
- 用較小的型別（`uint8_t`/`int16_t`）讓更多資料進快取（注意溢位）。
- 多執行緒各自的資料用 `_Alignas(64)` 隔開，避免偽共享（實測 0.049 s → 0.007 s）。

### 層級 3：編譯器（收益：1.2–5 倍，幾乎免費）

- 確認是 Release（SDK 預設 `-O3`），不是 Debug（`-Og`）。Linux 自己編時加 `-O2` 或 `-O3`，兩個都量。
- `restrict` 告訴編譯器指標不重疊（實測少一個迴圈內 `lw`）。
- 不變的運算手動提出迴圈；`static inline` 小函式。
- 用 `__builtin_popcount/clz/ctz/bswap32` 讓編譯器選最好的指令。
- 除以常數不用手改（編譯器已轉成乘法）；除以**同一個變數**很多次才手動改成倒數乘法。
- 看組合語言確認（`.dis` 檔、`objdump -d`、`gcc -S`）。

### 層級 4：微架構（收益：10–50%）

- 減少分支：`min`/`max`、`x & -(cond)`；但條件可預測時分支反而快。
- 展開迴圈 + 多個累加器打斷相依鏈。
- 避免除法與取模（Hazard3 除法約 18–19 週期、乘法 1 週期）；2 的次方用 `& (N-1)` 和 `>>`。
- RISC-V 沒有 FPU：改定點數（Q16.16）或整數；Arm 用 `float` 不要 `double`，常數寫 `1.0f`。
- 查表取代昂貴函式（sin、CRC），但 CPU 有單一指令能做的不要查表（[solutions.md](solutions.md) S10 實測）。
- MCU：熱函式放 SRAM（`__not_in_flash_func`），避免 flash XIP 快取失誤。

### 層級 5：平行（收益：最多約等於核心數）

- 先確認工作可以切成獨立的區段。
- 每個工作者用區域變數累加，最後合併一次（實測比每次上鎖快很多）。
- 臨界區只包共享資料那一行（實測 87 ms → 43 ms）。
- 單一計數器用 atomic；生產者/消費者用 SPSC 無鎖佇列。
- 執行緒數 = 核心數；RP2350 只有 2 顆核心。
- 工作太小時，派工成本會吃掉收益。

### 層級 6：硬體卸載（RP2350，收益：CPU 空出來）

- 搬資料 → DMA（[solutions.md](solutions.md) S01–S02）。
- 腳位時序、資料格式轉換 → PIO（S03–S04）。
- CRC / 加總 → DMA sniffer（S05）。
- 搬下一塊的同時算上一塊 → DMA 乒乓雙緩衝（[optimization.md](optimization.md) 第 5.1 節）。
- 頻繁小事件用 IRQ 會被進出成本拖慢 → 用 DMA 搬一大塊，完成才中斷一次。

### 層級 7：系統設定（收益：與設定成正比，先確認規則允許）

- 提高時脈：`set_sys_clock_khz`（電壓先升，`vreg_set_voltage`）。
- Linux：快速 I/O（下面的範本實測 0.40 s → 0.17 s）、綁核心（`taskset`）。
- ROS 2：拿掉 `print`、重用訊息物件、QoS 配對正確、必要時改 C++。

---

## 5. 驗證：快但錯 = 0 分

1. **保留 ref**：原版函式不刪，新版取名 `xxx_fast`。
2. **隨機對拍**：用隨機輸入跑兩個版本，比對每一個輸出（`cmp`、`diff`，或程式內 `memcmp`）。至少幾千組。
3. **邊界輸入**：0 個元素、1 個、2 個、奇數個、不是 4 的倍數、最大值、負數、全部相同、已排序、反向排序、`INT_MIN`/`INT_MAX`、`0xFFFFFFFF`。
4. **溢位**：加總用 `int64_t`；乘法前先轉型；`i * 37` 這種在大 i 會溢位（GCC 實際警告過）。
5. **多執行緒**：跑 10 次結果都要一樣；Linux 可以用 `-fsanitize=thread` 抓資料競爭。
6. **DMA / 硬體**：等「收」完再讀結果；比對整個輸出緩衝區，不是只看前幾個。

---

## 6. 卡住了？依序試這 20 件事

1. 確認編譯等級是 Release / `-O2` 以上，不是 Debug。
2. 拿掉熱迴圈裡所有 `printf`、`print`、log、`sleep`。
3. 找出最久的那段（量！），只改那段。
4. 看輸入範圍，對照第 1 節表格：複雜度夠不夠？不夠就換演算法。
5. 題目像數學或圖論 → 查 [algorithms.md](algorithms.md)。
6. 把重複計算的東西提到迴圈外（不變式、長度、指標）。
7. 迴圈裡的除法、取模 → 移位、遮罩、倒數乘法。
8. 浮點（尤其 RISC-V）→ 整數或定點數。
9. 改迴圈順序讓最內層循序存取。
10. 加 `restrict`、`static inline`。
11. 用 `__builtin_*` 位元指令。
12. 展開迴圈 + 兩到四個累加器。
13. 小範圍輸入 → 查表；可以預先算好 → 開機時建表。
14. 排序換成計數 / 基數排序；搜尋換成二分或雜湊。
15. 搬資料 → DMA；跟 PIO 一筆一筆來回 → DMA 管線。
16. 切給第二顆核心或多執行緒；各自累加最後合併。
17. 鎖縮到只包共享資料那一行；或改 atomic、SPSC 佇列。
18. MCU 熱程式碼放 SRAM（`__not_in_flash_func`、`copy_to_ram`）。
19. Linux 輸入輸出改 `fread`/`fwrite` 整塊處理（第 8 節範本）。
20. 規則允許的話提高時脈；不允許就回到第 3 步找下一個最久的段落。

---

## 7. 不要做的事

- 沒量就改；一次改很多件事（變慢了不知道是哪個）。
- 為了快犧牲正確（例如拿掉必要的鎖、等 TX 完成就讀 RX）。
- 用 `-march=native` 編 OJ 程式（評分機器 CPU 可能不同 → 非法指令）。
- 優化只占 5% 時間的程式碼。
- 相信小模型說的函式名稱而不查（用 `Ctrl+F` 在 [cheatsheet.md](cheatsheet.md) 搜）。
- 同時多次提交（考試規則禁止）。

---

## 8. Linux OJ 快速輸入輸出（實測）

```c
// Linux OJ 快速輸入輸出：一次 fread 整塊、手動解析整數；輸出先寫進緩衝區最後一次 fwrite
#include <stdio.h>
#include <stdint.h>
static char ibuf[1 << 16]; static size_t ipos, ilen;
static inline int gc(void) {
    if (ipos == ilen) { ilen = fread(ibuf, 1, sizeof ibuf, stdin); ipos = 0; if (!ilen) return -1; }
    return ibuf[ipos++];
}
static inline int read_i64(int64_t *out) {            // 讀到 EOF 回傳 0
    int c = gc(); while (c != '-' && (c < '0' || c > '9')) { if (c < 0) return 0; c = gc(); }
    int neg = c == '-'; if (neg) c = gc();
    int64_t v = 0; while (c >= '0' && c <= '9') { v = v * 10 + (c - '0'); c = gc(); }
    *out = neg ? -v : v; return 1;
}
static char obuf[1 << 16]; static size_t opos;
static inline void flush_out(void) { fwrite(obuf, 1, opos, stdout); opos = 0; }
static inline void write_i64(int64_t v, char end) {
    if (opos > sizeof obuf - 24) flush_out();
    if (v < 0) { obuf[opos++] = '-'; v = -v; }
    char t[20]; int n = 0; do { t[n++] = (char)('0' + v % 10); v /= 10; } while (v);
    while (n) obuf[opos++] = t[--n];
    obuf[opos++] = end;
}
int main(void) {                                       // 範例：每個數乘 2 輸出
    int64_t x;
    while (read_i64(&x)) write_i64(x * 2, '\n');
    flush_out();
}
```

實測 300 萬個整數（每個乘 2 輸出）：`scanf`/`printf` 0.40 s → 這個版本 0.17 s，輸出完全相同。注意：`write_i64` 不處理 `INT64_MIN`；若題目可能出現，先轉 `uint64_t` 再處理。

---

## 9. 考試當天時間分配建議

| 時間 | 做什麼 |
|---|---|
| 09:00–09:40 | 讀完所有題目與文件，標出每題的「輸入範圍、評分方式、給的 API」，對照 [solutions.md](solutions.md) 速查表 |
| 09:40–12:00 | 每題先交一個**正確**的版本（拿基本分），再挑收益最大的優化 |
| 13:00–16:00 | 照第 0 節循環：量 → 改一件 → 比對 → 量 |
| 16:00 之後 | 不再大改；確認每題最後交的版本是正確且最快的那個 |
