# 演算法級優化儲備：數論・圖論・資料結構・DP・字串（上機考用）

微優化（編譯旗標、記憶體放置、DMA、PIO）在 [optimization.md](optimization.md)；這份只管「換演算法」：同一題把 O(n²) 變 O(n log n)、把除法變乘法、把暴力變公式。這種改法常常一次就快 10～1000 倍，比任何微調都大。

**怎麼驗證的（2026-11-13）**
- 每個範本都是 C11，只用 `<stdint.h>`、`<string.h>` 和 GCC/Clang 內建函式（`__builtin_ctz` 等），全部是靜態陣列、不用 malloc、圖論 DFS 全部是迭代版（不遞迴）。
- 全部在 Linux 上用 `gcc -O2 -Wall -Wextra` 編譯，並用隨機輸入對照暴力解：**總共 0 筆不同**。
- 全部用 `riscv64-unknown-elf-gcc 13.2 -march=rv32imac_zicsr_zifencei_zba_zbb_zbs_zbkb -mabi=ilp32`（RP2350 RISC-V 核心的指令集）與 `clang 18 --target=thumbv8m.main -mcpu=cortex-m33`（RP2350 Arm 核心）編譯通過，沒有警告。
- 加速倍數：標「RV32」的是用 qemu-riscv32 逐指令計數，在 RP2350 指令集下算出來的**估計週期**（除法以 18 週期計、其餘 1 週期；Hazard3 實際週期 未驗證）；其他是 x86 主機 `-O2` 實測時間比。演算法級的倍數主要來自複雜度，換到 MCU 上比例大致相同。
- 沒有上 Pico 2 實機。
- 範本原始檔與測試程式在 [algorithms-tests/](algorithms-tests/)：`gcc -O2 test_dp.c -o t && ./t`，最後一行印 `fails=0` 就是全部通過（`test_nt.c` 含 π(2³²) 約要 1 分鐘）。

---

## 目錄

0. [考場用法](#0-考場用法)
1. [對照表：題目長這樣 → 用這招](#1-對照表題目長這樣--用這招)
2. [RP2350 上什麼東西貴](#2-rp2350-上什麼東西貴)
3. [數論範本](#3-數論範本)
4. [排序與選擇](#4-排序與選擇)
5. [圖論範本](#5-圖論範本)
6. [區間查詢與資料結構](#6-區間查詢與資料結構)
7. [DP 與搜尋技巧](#7-dp-與搜尋技巧)
8. [字串與卷積](#8-字串與卷積)
9. [找規律：數學題的硬解方法](#9-找規律數學題的硬解方法)
10. [給小模型的 prompt](#10-給小模型的-prompt)
11. [RAM 預算與未收錄清單](#11-ram-預算與未收錄清單)

---

## 0. 考場用法

1. **先分類，再寫程式**：讀題後到第 1 節找最像的一列。找不到就用第 10.1 節的 prompt 讓小模型分類。
2. **先寫暴力版**：最直接的寫法，n 小時當標準答案。
3. **貼範本**：從第 3～8 節複製，只改輸入輸出與陣列大小（每段開頭寫了 RAM 用量與相依的其他範本）。
4. **對拍**：隨機小輸入跑暴力版與新版，結果必須逐筆相同，再拿大輸入計時。
5. **陣列大小先算 RAM**：RP2350 只有 520 KB SRAM，堆疊只有幾 KB。範本裡的 `#define` 大小照題目上限調，總和留 50 KB 以上給 SDK 與堆疊。

所有範本的共同開頭：

```c
#include <stdint.h>
#include <string.h>
```

---

## 1. 對照表：題目長這樣 → 用這招

「加速」欄：RV32 = RP2350 指令集估計週期比；其他為主機實測。小模型分類用的精簡版在第 10.1 節。

### 數論

| 題目長這樣 | 用這招（節） | 複雜度 | 實測加速 |
|---|---|---|---|
| 迴圈裡一直 `x * y % p`（同一個 p） | Montgomery 乘法（3.9） | 每次少一個 64 位元除法函式呼叫 | 模乘 4096 次 **RV32 ×5.9** |
| 快速冪 `a^e mod p` 做很多次 | Montgomery 版快速冪（3.9） | O(log e)，迴圈內 0 次除法 | **RV32 ×8.0** |
| 64 位元數 `% m`，m 是執行期常數 | Barrett（3.10） | 4 次乘法取代 `__umoddi3` | 模乘鏈 **RV32 ×4.5** |
| 32 位元 `a / d`、`a % d`，d 執行期才知道但迴圈內不變 | Lemire fastdiv/fastmod（3.11） | 除法 → 乘法 | **RV32 ×2.1** |
| 大量 gcd | 二進位 GCD（3.4） | 沒有除法 | **RV32 ×3.0**（x86 上反而慢 15～20%，只在 MCU 用） |
| 判斷一個大數是不是質數 | Miller–Rabin（3.7） | O(√n) → O(log n) | 31 位元數 **RV32 ×43** |
| 1..N 有幾個質數 / 很多次問質數 | 埃氏篩、位元篩（3.5） | O(N√N) → O(N log log N) | N=3 萬 **RV32 ×25**；π(10⁶) 逐一 Miller–Rabin vs 篩 主機 ×72 |
| N 大到 10⁸～2³² 的質數計數 | 分段篩（3.5） | RAM 從 N/16 降到約 75 KB | π(10⁸) 約 0.09 s、π(2³²−1) 約 8 s（主機） |
| 1..N 每個數的 φ、μ、最小質因數 | 線性篩（3.6） | O(N) | — |
| 分解 n < 2³² | 6k±1 試除（3.8） | O(√n) | — |
| 分解 64 位元大數 | Pollard rho + 64 位元 Miller–Rabin（3.17） | O(n^¼) | 3000 個 < 2⁶³ 隨機數共 0.027 s |
| 求 x 使 a^x ≡ b (mod p) | BSGS 大步小步（3.18） | O(p) → O(√p) | — |
| 一組「除以 m 餘 a」條件求最小數 | CRT 合併（3.13） | O(log m) | — |
| 很多次 C(n,r) mod p | 階乘表 + 反元素（3.12） | 每次 O(1) | — |
| C(n,r)，n 到 10¹⁸、p 小 | Lucas（3.12） | O(p + log_p n) | — |
| 線性遞迴第 n 項，n 到 10¹⁸ | 矩陣快速冪 / Fibonacci 倍增（3.14） | O(n) → O(K³ log n) | F(10⁸) 主機 **約 ×30 萬** |
| Σ floor(n/i)、因數和總和、n 到 10¹² | 整除分塊（3.16） | O(n) → O(√n) | n=10⁹ 主機 **×5,500** |
| 「重複套用 f 第 10¹⁸ 次」「LCG 第 n 項」 | 循環偵測 Brent（3.15） | O(n) → O(μ+λ) | — |
| 1..n 中能被 a₁..a_k 任一整除的個數 | 排容原理（3.16） | O(n k) → O(2^k k) | — |
| 0..n 所有數 popcount 總和、第 k 位元有幾個 1 | 位元公式（3.15） | O(n) → O(log n) | — |
| 等比級數和 mod m（m 不是質數） | 倍增求和（3.16） | O(n) → O(log n)，不需反元素 | — |
| 開根號（沒有 FPU） | 整數開根號（3.1） | 純整數 | — |

### 圖論

| 題目長這樣 | 用這招（節） | 複雜度 | 實測加速 |
|---|---|---|---|
| 所有圖題的存法 | CSR 陣列（5.1） | 沒有 malloc、連續記憶體 | — |
| 無權最短路、可達、連通塊 | BFS（5.2） | O(N+M) | — |
| 非負權最短路 | Dijkstra + 打包 uint64 堆積（5.5） | O(N²) → O((N+M) log M) | N=2萬 M=10萬 主機 **×307** |
| 權重只有 0 / 1 | 0-1 BFS（5.6） | 少一個 log | — |
| 有負權、要偵測負環 | Bellman–Ford 提早結束（5.7） | O(NM) | — |
| 全點對最短路，N ≤ 200 | Floyd–Warshall（5.8） | O(N³) | — |
| 只問「誰能到誰」 | 位元集合遞移閉包（5.9） | O(N³) → O(N³/32) | N=1000 主機 **×23** |
| 稠密圖（鄰接矩陣）BFS | 位元集合 BFS（5.10） | O(N²) → O(N²/32) | — |
| 依賴順序、有沒有環、DAG 上最長路 | Kahn 拓樸排序（5.4） | O(N+M) | — |
| 動態合併、問是否同一群、無向找環 | 並查集（5.11） | 近 O(1) | — |
| 最小生成樹 | Kruskal + 基數排序（5.12）；稠密圖用 Prim O(N²)（5.13） | O(M log M) → O(M α) | — |
| 能不能分兩組、有沒有奇環 | 二分圖著色（5.14） | O(N+M) | — |
| 互相可達的群組、縮點、2-SAT | Tarjan SCC 迭代版（5.15） | O(N+M) | — |
| 拿掉哪條邊/點會斷開 | 橋與割點（5.16） | O(M·(N+M)) → O(N+M) | — |
| 樹上兩點距離、祖先、LCA | 跳躍指標 LCA（5.17） | 每次 O(深度) → O(log N)，RAM O(N) | 鏈狀樹 N=2萬 主機 **×83** |
| 最大流、最小割、二分匹配、指派 | Dinic（5.18） | 匹配 O(E√V) | 900×900 匹配 0.8 ms（主機） |
| 方格迷宮最短路 | 補邊界 BFS（5.19） | O(HW)，1 byte/格 | — |
| 方格、終點離起點近 | A*（5.20） | 只展開往終點的格子 | 近終點 ×2.1；**終點很遠時 A* 慢 4 倍，用 BFS** |

### 區間、資料結構、排序

| 題目長這樣 | 用這招（節） | 複雜度 | 實測加速 |
|---|---|---|---|
| 陣列不變、問很多次區間和 | 前綴和 / 2D 前綴和（6.1） | 每次 O(n) → O(1) | 1000 次查詢 n=10⁵ 主機 **×39** |
| 很多次「區間全部加 v」，最後才看結果 | 差分陣列 / 2D 差分（6.2） | 每次 O(n) → O(1) | — |
| 邊改邊問區間和、逆序數對、第 k 小 | Fenwick 樹（6.3） | O(n) → O(log n) | 逆序數 n=5萬 主機 **×379** |
| 邊改邊問區間 min/max/gcd | 非遞迴線段樹（6.4） | O(n) → O(log n) | 2000 次 n=65536 主機 **×93** |
| 陣列不變、問很多次區間 min | Sparse table（6.5） | 每次 O(1) | — |
| 固定長度視窗的最大/最小 | 單調雙端佇列（6.6） | O(nk) → O(n) | k=1000 主機 **×56** |
| 右邊第一個更大、直方圖最大矩形 | 單調堆疊（6.6） | O(n²) → O(n) | — |
| 非負陣列「和 ≤ S 的最長子陣列」、排序陣列找數對 | 雙指標（6.7） | O(n²) → O(n) | — |
| 最大連續子陣列和 | Kadane（6.7） | O(n²) → O(n) | — |
| 排序整數 | 基數排序（4.1）；值域小用計數排序（4.2） | O(n log n) → O(n) | n=20萬 vs qsort 主機 **×15** |
| 第 k 小、中位數、前 k 大 | Quickselect（4.3） | O(n log n) → 平均 O(n) | 中位數 n=20萬 主機 **×12.7** |
| 「最小的最大值」「最少幾天」這類單調條件 | 對答案二分（6.8） | 窮舉 → O(log 範圍 × 檢查) | — |
| 值域很大但個數少，要開陣列 | 座標壓縮（7.7） | — | — |

### DP、搜尋、字串

| 題目長這樣 | 用這招（節） | 複雜度 | 實測加速 |
|---|---|---|---|
| 最長遞增子序列 | 二分 + tail 陣列（7.1） | O(n²) → O(n log n) | n=2萬 主機 **×902** |
| 背包 | 一維 DP（7.2） | 記憶體 O(nC) → O(C) | — |
| 只問「湊不湊得到總和 C」 | 位元集合子集和（7.2） | O(nC) → O(nC/32) | n=200 C=10⁵ 主機 **×13** |
| n ≤ 12 個點全部走一遍（TSP、排列最佳化） | 位元 DP（7.3） | O(n!) → O(2ⁿ n²) | n=11 主機 **×173** |
| 每個集合對其所有子集求和 | SOS DP（7.4） | O(3ⁿ) → O(n 2ⁿ) | — |
| n ≤ 30～40 的子集枚舉 | 折半搜尋（7.5） | O(2ⁿ) → O(2^(n/2)) | n=26 主機 **×1,561** |
| 字串裡找樣式（所有出現位置） | KMP / Z 函數（8.1、8.2） | O(nm) → O(n+m) | 最壞情況 主機 ×9 |
| 很多次比較子字串是否相等、最長重複子字串 | 滾動雜湊，模 2³¹−1 無除法（8.3） | 每次 O(len) → O(1) | — |
| 多項式相乘、大數乘法、「兩數和為 s 的組數」對所有 s | NTT 卷積（8.4） | O(n²) → O(n log n) | 16000×16000 主機 **×44** |
| 只給小 n 的答案、要推大 n | 暴力打表找規律（第 9 節） | 變成公式 O(1) | — |

---

## 2. RP2350 上什麼東西貴

RISC-V 核心（Hazard3）有乘法和 32 位元除法指令，但**沒有** 64 位元除法、沒有 FPU。`uint64_t % m` 會變成 libgcc 的 `__umoddi3` 函式呼叫（內部是好幾次 32 位元除法加迴圈）。Arm 核心（Cortex-M33）的 `udiv` 比較快，但同樣沒有 64 位元除法（變成 `__aeabi_uldivmod`）。

用 qemu-riscv32 逐指令計數（RP2350 指令集、GCC -O2），每項跑同一組隨機輸入、結果相同：

| 工作 | 原寫法 | 改寫 | 指令數 | 除法指令數 | 估計週期（除法 18） | 倍數 |
|---|---|---|---|---|---|---|
| 2048 次 gcd | 歐幾里得 `%` | 二進位 GCD | 172,794 → 278,828 | 38,590 → 0 | 828,824 → 278,828 | ×3.0 |
| 4096 次模乘鏈 mod 1e9+7 | `uint64 %` | Montgomery | 459,805 → 103,156 | 32,768 → 4,096 | 1,016,861 → 172,788 | ×5.9 |
| 同上 | `uint64 %` | Barrett | 459,805 → 225,563 | 32,768 → 0 | 1,016,861 → 225,563 | ×4.5 |
| 4096 次 `a/d + a%d`（d 執行期常數） | `/`、`%` | fastdiv/fastmod | 40,966 → 86,110 | 8,192 → 8 | 180,230 → 86,246 | ×2.1 |
| 64 次 `a^e mod p`（e 32 位元） | `pow_mod` | `pow_mont` | 189,686 → 47,993 | 12,064 → 64 | 394,774 → 49,081 | ×8.0 |
| 64 個 31 位元奇數判質數 | 試除到 √n | Miller–Rabin | 2,380,380 → 95,620 | 396,658 → 6,773 | 9,123,566 → 210,761 | ×43 |
| 數 0..30000 的質數 | 每個試除 | 埃氏篩 | 3,196,300 → 458,908 | 493,932 → 0 | 11,593,144 → 458,908 | ×25 |

讀法：
- **指令數變多不代表變慢**：fastdiv、Stein 指令數比較多，但沒有除法。除法週期數一變，倍數就變；Hazard3 實際除法週期 未驗證（optimization.md 寫約 18–19）。
- 在 x86 主機上 Stein GCD 比 `%` 版慢 15～20%、Montgomery 也沒比較快，因為 x86 除法很快。**不要用電腦上的計時判斷 MCU 上的微優化**，演算法級的改變（複雜度）才會兩邊一致。
- 64 位元 `__builtin_ctzll` 在 RV32 GCC 13 也會變成 `__ctzdi2` 函式呼叫；32 位元 `__builtin_ctz`、`__builtin_popcount` 有 Zbb 時是單一指令（`ctz`、`cpop`）。

---

## 3. 數論範本

### 3.1 整數開根號（沒有 FPU 時取代 `sqrt`）

```c
// floor(sqrt(n))，純整數（RISC-V 沒有 FPU 也能用）
static uint32_t isqrt32(uint32_t n) {
    uint32_t r = 0, bit = 1u << 30;
    while (bit > n) bit >>= 2;
    while (bit) {
        if (n >= r + bit) { n -= r + bit; r = (r >> 1) + bit; }
        else r >>= 1;
        bit >>= 2;
    }
    return r;
}
static uint64_t isqrt64(uint64_t n) {
    uint64_t r = 0, bit = 1ull << 62;
    while (bit > n) bit >>= 2;
    while (bit) {
        if (n >= r + bit) { n -= r + bit; r = (r >> 1) + bit; }
        else r >>= 1;
        bit >>= 2;
    }
    return r;
}
```

### 3.2 快速冪

```c
// a^e mod m，m < 2^32，O(log e)。注意 RV32 上 uint64 % 是函式呼叫(__umoddi3)，熱迴圈改用 Montgomery
static uint32_t pow_mod(uint32_t a, uint64_t e, uint32_t m) {
    uint64_t r = 1 % m, b = a % m;
    while (e) {
        if (e & 1) r = r * b % m;
        b = b * b % m;
        e >>= 1;
    }
    return (uint32_t)r;
}
```

### 3.3 模反元素（需要 3.2）

```c
// p 是質數、a % p != 0：a^(p-2) mod p
static uint32_t inv_fermat(uint32_t a, uint32_t p) { return pow_mod(a, p - 2, p); }
// 任意 m：gcd(a,m)=1 才有反元素，沒有就回傳 0（迭代延伸歐幾里得，不遞迴）
static uint32_t inv_exgcd(uint32_t a, uint32_t m) {
    int64_t r0 = a % m, r1 = m, s0 = 1, s1 = 0;
    while (r1) {
        int64_t q = r0 / r1, t;
        t = r0 - q * r1; r0 = r1; r1 = t;
        t = s0 - q * s1; s0 = s1; s1 = t;
    }
    if (r0 != 1) return 0;
    s0 %= (int64_t)m; if (s0 < 0) s0 += m;
    return (uint32_t)s0;
}
// 1..n 的反元素一次算完，O(n)：inv[i] = -(p/i) * inv[p%i]。p 質數、n < p
static void inv_table(uint32_t *inv, uint32_t n, uint32_t p) {
    inv[1] = 1;
    for (uint32_t i = 2; i <= n; i++)
        inv[i] = (uint32_t)((uint64_t)(p - p / i) * inv[p % i] % p);
}
```

### 3.4 GCD / LCM

```c
// 二進位 GCD (Stein)：只有位移與減法，ctz 在 Hazard3(Zbb) 是一道指令
static uint32_t gcd_u32(uint32_t a, uint32_t b) {
    if (!a) return b;
    if (!b) return a;
    int sh = __builtin_ctz(a | b);
    a >>= __builtin_ctz(a);
    do {
        b >>= __builtin_ctz(b);
        if (a > b) { uint32_t t = a; a = b; b = t; }
        b -= a;
    } while (b);
    return a << sh;
}
static uint64_t lcm_u32(uint32_t a, uint32_t b) {   // 先除再乘，避免溢位
    return (a && b) ? (uint64_t)(a / gcd_u32(a, b)) * b : 0;
}
```

### 3.5 篩法：位元篩與分段篩（分段篩需要 3.1）

```c
// 只存奇數的位元篩：bit i 代表 2i+1。RAM = N/16 bytes（N=4e6 → 250 KB）
#define SIEVE_N 1000000u
static uint32_t sieve_bits[(SIEVE_N / 2) / 32 + 1];   // 1 = 合數
static void sieve_odd(void) {
    memset(sieve_bits, 0, sizeof sieve_bits);
    sieve_bits[0] |= 1u;                                // 1 不是質數
    for (uint32_t i = 3; i * i <= SIEVE_N; i += 2)
        if (!((sieve_bits[i >> 6] >> ((i >> 1) & 31)) & 1))
            for (uint32_t j = i * i; j <= SIEVE_N; j += 2 * i)
                sieve_bits[j >> 6] |= 1u << ((j >> 1) & 31);
}
static int is_prime_sieved(uint32_t n) {          // n <= SIEVE_N
    if (n < 2) return 0;
    if (!(n & 1)) return n == 2;
    return !((sieve_bits[n >> 6] >> ((n >> 1) & 31)) & 1);
}
static uint32_t count_primes_sieved(uint32_t n) { // π(n)，用 popcount 一次數 32 個
    if (n < 2) return 0;
    uint32_t last = (n - 1) >> 1, c = 1, w;       // c=1 先算 2
    for (w = 0; w < (last >> 5); w++) c += __builtin_popcount(~sieve_bits[w]);
    uint32_t rem = (last & 31) + 1;
    uint32_t mask = rem == 32 ? 0xFFFFFFFFu : (1u << rem) - 1;
    return c + __builtin_popcount(~sieve_bits[w] & mask);
}
```

```c
// 分段篩：π(N)，N 可到 2^32-1，RAM 只要約 75 KB（整段篩 N=1e8 要 6 MB，放不下）
#define SEG 16384u
static uint32_t count_primes_seg(uint32_t N) {
    static uint8_t seg[SEG], small[32768];       // seg[i]: 奇數 2(lo+i)+1；small[i]: 2i+1
    static uint32_t bp[6542];
    uint32_t nbp = 0, r = isqrt32(N), cnt = 1;
    if (N < 2) return 0;
    memset(small, 0, sizeof small);
    for (uint32_t i = 3; i <= r; i += 2)
        if (!small[i >> 1]) {
            bp[nbp++] = i;
            for (uint32_t j = i * i; j <= r; j += 2 * i) small[j >> 1] = 1;
        }
    uint32_t kmax = (N - 1) / 2;                 // 奇數 2k+1, k = 1..kmax
    for (uint32_t lo = 1; lo <= kmax; lo += SEG) {
        uint32_t hi = (kmax - lo < SEG - 1) ? kmax : lo + SEG - 1;
        memset(seg, 0, hi - lo + 1);
        for (uint32_t t = 0; t < nbp; t++) {
            uint64_t p = bp[t], s = p * p, lowv = 2ull * lo + 1;
            if (s > 2ull * hi + 1) break;
            if (s < lowv) { s = (lowv + p - 1) / p * p; if (!(s & 1)) s += p; }
            for (uint32_t k = (uint32_t)((s - 1) / 2); k <= hi; k += (uint32_t)p) seg[k - lo] = 1;
        }
        for (uint32_t i = 0; i <= hi - lo; i++) cnt += !seg[i];
        if (hi == kmax) break;
    }
    return cnt;
}
```

### 3.6 線性篩（φ、μ、最小質因數一次算完）

```c
// 線性篩 O(N)：同時得到質數表、最小質因數 spf、歐拉 φ、Möbius μ
// RAM：spf 4N + phi 4N + mu N bytes。N=40000 → 約 380 KB。不需要的陣列刪掉
#define LN 40000u
static uint32_t lp_primes[4300], lp_np;          // π(40000)=4203
static uint32_t spf[LN + 1], phi[LN + 1];
static int8_t mu[LN + 1];
static void linear_sieve(void) {
    lp_np = 0; phi[1] = 1; mu[1] = 1;
    for (uint32_t i = 2; i <= LN; i++) {
        if (!spf[i]) { spf[i] = i; lp_primes[lp_np++] = i; phi[i] = i - 1; mu[i] = -1; }
        for (uint32_t j = 0; j < lp_np; j++) {
            uint32_t p = lp_primes[j], k = i * p;
            if (p > spf[i] || k > LN) break;
            spf[k] = p;
            if (p == spf[i]) { phi[k] = phi[i] * p; mu[k] = 0; }
            else { phi[k] = phi[i] * (p - 1); mu[k] = (int8_t)-mu[i]; }
        }
    }
}
// 有 spf 之後，分解 n <= LN 只要 O(質因數個數)
```

### 3.7 Miller–Rabin（32 位元，需要 3.2）

```c
// 32 位元確定性質數判斷：底數 {2,7,61} 對所有 n < 4,759,123,141 正確
static int is_prime_u32(uint32_t n) {
    static const uint8_t sp[] = {2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37};
    static const uint32_t base[] = {2, 7, 61};
    if (n < 2) return 0;
    for (int i = 0; i < 12; i++) {
        if (n == sp[i]) return 1;
        if (n % sp[i] == 0) return 0;
    }
    if (n < 41 * 41) return 1;
    uint32_t d = n - 1;
    int s = __builtin_ctz(d);
    d >>= s;
    for (int i = 0; i < 3; i++) {
        uint64_t x = pow_mod(base[i], d, n);
        if (x == 1 || x == n - 1) continue;
        int ok = 0;
        for (int r = 1; r < s && !ok; r++) { x = x * x % n; ok = (x == n - 1); }
        if (!ok) return 0;
    }
    return 1;
}
```

### 3.8 分解質因數、φ(n)、因數個數

```c
// 試除法分解 n < 2^32（最多 9 個不同質因數）；O(sqrt n) 最壞約 2 萬次除法
static int factor_u32(uint32_t n, uint32_t p[10], uint8_t e[10]) {
    int k = 0;
    if (n < 2) return 0;
    for (uint32_t d = 2; d <= 3; d++)
        if (n % d == 0) { p[k] = d; e[k] = 0; while (n % d == 0) { n /= d; e[k]++; } k++; }
    for (uint32_t d = 5; (uint64_t)d * d <= n; d += 6)       // 6k±1
        for (uint32_t q = d; q <= d + 2; q += 2)
            if (n % q == 0) { p[k] = q; e[k] = 0; while (n % q == 0) { n /= q; e[k]++; } k++; }
    if (n > 1) { p[k] = n; e[k] = 1; k++; }
    return k;
}
// 由分解得到：因數個數 Π(e+1)、因數和 Π(p^(e+1)-1)/(p-1)、φ(n) = n Π(1-1/p)
static uint32_t phi_u32(uint32_t n) {
    uint32_t p[10]; uint8_t e[10];
    int k = factor_u32(n, p, e);
    uint32_t r = n;
    for (int i = 0; i < k; i++) r = r / p[i] * (p[i] - 1);
    return r;
}
static uint32_t num_divisors(uint32_t n) {
    uint32_t p[10], r = 1; uint8_t e[10];
    int k = factor_u32(n, p, e);
    for (int i = 0; i < k; i++) r *= e[i] + 1u;
    return r;
}
```

### 3.9 Montgomery 乘法（取代迴圈內的 `%`）

```c
// Montgomery 乘法：m 奇數且 < 2^31。只用 32x32→64 乘法，不需要任何除法
// 用法：先 to_mont 轉進去，迴圈內都用 mont_mul，最後 from_mont 轉回來
typedef struct { uint32_t m, minv, r2; } mont_t;   // minv = -m^{-1} mod 2^32, r2 = 2^64 mod m
static mont_t mont_init(uint32_t m) {
    mont_t M; uint32_t inv = m;                    // 奇數 m：m*m ≡ 1 (mod 8)，牛頓法每次精度加倍
    for (int i = 0; i < 4; i++) inv *= 2 - m * inv;
    M.m = m; M.minv = 0u - inv;
    M.r2 = (uint32_t)((0ull - m) % m);             // 只在初始化做一次 64 位元 %
    return M;
}
static inline uint32_t mont_redc(const mont_t *M, uint64_t T) {   // T < m*2^32 → T/R mod m
    uint32_t u = (uint32_t)T * M->minv;
    uint32_t t = (uint32_t)((T + (uint64_t)u * M->m) >> 32);
    return t >= M->m ? t - M->m : t;
}
static inline uint32_t mont_mul(const mont_t *M, uint32_t a, uint32_t b) { return mont_redc(M, (uint64_t)a * b); }
static inline uint32_t to_mont(const mont_t *M, uint32_t a) { return mont_mul(M, a % M->m, M->r2); }
static inline uint32_t from_mont(const mont_t *M, uint32_t a) { return mont_redc(M, a); }
static uint32_t pow_mont(const mont_t *M, uint32_t a, uint64_t e) {
    uint32_t r = to_mont(M, 1), b = to_mont(M, a);
    while (e) { if (e & 1) r = mont_mul(M, r, b); b = mont_mul(M, b, b); e >>= 1; }
    return from_mont(M, r);
}
```

### 3.10 Barrett（64 位元數 mod 32 位元執行期常數）

```c
// Barrett：x mod m，x < 2^64、m 執行期常數(>=1)。4 次 32x32 乘法取代 __umoddi3
static inline uint64_t mulhi64(uint64_t a, uint64_t b) {         // (a*b) >> 64，不用 __int128
    uint64_t al = (uint32_t)a, ah = a >> 32, bl = (uint32_t)b, bh = b >> 32;
    uint64_t ll = al * bl, lh = al * bh, hl = ah * bl, hh = ah * bh;
    uint64_t mid = (ll >> 32) + (uint32_t)lh + (uint32_t)hl;
    return hh + (lh >> 32) + (hl >> 32) + (mid >> 32);
}
typedef struct { uint64_t mu; uint32_t m; } barrett_t;
static barrett_t barrett_init(uint32_t m) { barrett_t B; B.mu = UINT64_MAX / m; B.m = m; return B; }
static inline uint32_t barrett_mod(const barrett_t *B, uint64_t x) {
    uint64_t r = x - mulhi64(x, B->mu) * B->m;                  // r < 3m
    while (r >= B->m) r -= B->m;
    return (uint32_t)r;
}
```

### 3.11 除以執行期常數：fastdiv / fastmod

```c
// Lemire fastmod/fastdiv：32 位元 a 除以執行期常數 d (d >= 2)，結果完全正確
// 迴圈外 M = fd_init(d)；迴圈內 a/d、a%d 都只剩乘法
static inline uint64_t fd_init(uint32_t d) { return UINT64_MAX / d + 1; }
static inline uint32_t mulhi_64x32(uint64_t M, uint32_t a) {     // (M*a) >> 64
    return (uint32_t)(((M >> 32) * a + (((uint64_t)(uint32_t)M * a) >> 32)) >> 32);
}
static inline uint32_t fastdiv_u32(uint32_t a, uint64_t M) { return mulhi_64x32(M, a); }
static inline uint32_t fastmod_u32(uint32_t a, uint64_t M, uint32_t d) { return mulhi_64x32(M * a, d); }
// 編譯期常數除法編譯器自己會換成乘法，例如 x/10 == (uint32_t)(((uint64_t)x * 0xCCCCCCCDu) >> 35)
```

### 3.12 組合數 nCr（階乘表、Lucas、巴斯卡；需要 3.2）

```c
// nCr mod p（p 質數且 > FN）：O(FN) 預處理，查詢 O(1)。RAM = 8*FN bytes（FN=20000 → 160 KB）
#define FN 20000u
static uint32_t fact[FN + 1], ifact[FN + 1];
static void ncr_init(uint32_t p) {
    fact[0] = 1;
    for (uint32_t i = 1; i <= FN; i++) fact[i] = (uint32_t)((uint64_t)fact[i - 1] * i % p);
    ifact[FN] = pow_mod(fact[FN], p - 2, p);
    for (uint32_t i = FN; i > 0; i--) ifact[i - 1] = (uint32_t)((uint64_t)ifact[i] * i % p);
}
static uint32_t ncr(uint32_t n, uint32_t r, uint32_t p) {
    if (r > n) return 0;
    return (uint32_t)((uint64_t)fact[n] * ifact[r] % p * ifact[n - r] % p);
}
// n 很大、p 很小（例如 p < 1e5）：Lucas 定理
static uint32_t ncr_small(uint32_t n, uint32_t r, uint32_t p) {  // n, r < p，O(r)
    if (r > n) return 0;
    uint64_t num = 1, den = 1;
    for (uint32_t i = 0; i < r; i++) { num = num * (n - i) % p; den = den * (i + 1) % p; }
    return (uint32_t)(num * pow_mod((uint32_t)den, p - 2, p) % p);
}
static uint32_t lucas(uint64_t n, uint64_t r, uint32_t p) {
    uint64_t res = 1;
    while (n || r) {
        uint32_t ni = n % p, ri = r % p;
        if (ri > ni) return 0;
        res = res * ncr_small(ni, ri, p) % p;
        n /= p; r /= p;
    }
    return (uint32_t)res;
}
// 模數不是質數、n 小（<= 60 左右）：巴斯卡三角形一列，C[k] = C(n,k) mod m，任何 m
static void pascal_row(uint32_t n, uint32_t m, uint32_t *C) {
    C[0] = 1 % m;
    for (uint32_t i = 1; i <= n; i++) {
        C[i] = 0;
        for (uint32_t k = i; k > 0; k--) { C[k] += C[k - 1]; if (C[k] >= m) C[k] -= m; }
    }
}
```

### 3.13 中國剩餘定理（模數可不互質）

```c
// 沒有 __int128（RV32 GCC 不支援）時的 64 位元 a*b mod m，m < 2^63
static uint64_t mulmod_u64(uint64_t a, uint64_t b, uint64_t m) {
    uint64_t r = 0;
    a %= m; b %= m;
    while (b) {
        if (b & 1) { r += a; if (r >= m) r -= m; }
        a += a; if (a >= m) a -= m;
        b >>= 1;
    }
    return r;
}
static int64_t exgcd64(int64_t a, int64_t b, int64_t *x) {   // 回傳 g，且 a*x ≡ g (mod b)
    int64_t x0 = 1, x1 = 0;
    while (b) {
        int64_t q = a / b, t = a - q * b; a = b; b = t;
        t = x0 - q * x1; x0 = x1; x1 = t;
    }
    *x = x0;
    return a;
}
// 合併 x≡*a (mod *m) 與 x≡a2 (mod m2)，模數不必互質。無解回傳 0。結果 lcm 要 < 2^62
static int crt_merge(uint64_t *a, uint64_t *m, uint64_t a2, uint64_t m2) {
    uint64_t a1 = *a % *m, m1 = *m;
    a2 %= m2;
    int64_t x;
    uint64_t g = (uint64_t)exgcd64((int64_t)(m1 % m2), (int64_t)m2, &x);
    uint64_t diff = (a2 + m2 - a1 % m2) % m2;
    if (diff % g) return 0;
    uint64_t m2g = m2 / g;
    int64_t xm = x % (int64_t)m2g; if (xm < 0) xm += (int64_t)m2g;
    uint64_t t = mulmod_u64(diff / g, (uint64_t)xm, m2g);
    *m = m1 * m2g;
    *a = (a1 + mulmod_u64(m1, t, *m)) % *m;
    return 1;
}
```

### 3.14 矩陣快速冪、Fibonacci 倍增

```c
// 線性遞迴 a[n] = c1 a[n-1] + ... + cK a[n-MATK]，n 到 1e18：MATK×MATK 矩陣快速冪，O(MATK^3 log n)
#define MATK 3
typedef struct { uint32_t a[MATK][MATK]; } mat_t;
static mat_t mat_mul(const mat_t *x, const mat_t *y, uint32_t m) {
    mat_t r;
    for (int i = 0; i < MATK; i++)
        for (int j = 0; j < MATK; j++) {
            uint64_t s = 0;
            for (int k = 0; k < MATK; k++) s = (s + (uint64_t)x->a[i][k] * y->a[k][j]) % m;
            r.a[i][j] = (uint32_t)s;
        }
    return r;
}
static mat_t mat_pow(mat_t b, uint64_t e, uint32_t m) {
    mat_t r;
    memset(&r, 0, sizeof r);
    for (int i = 0; i < MATK; i++) r.a[i][i] = 1 % m;
    while (e) { if (e & 1) r = mat_mul(&r, &b, m); b = mat_mul(&b, &b, m); e >>= 1; }
    return r;
}
// 用法：轉移矩陣第一列 = {c1..cK}，下面是位移的單位矩陣；
// [a(n) a(n-1) .. a(n-MATK+1)]^T = T^(n-MATK+1) [a(MATK-1) .. a(0)]^T
// 只要 Fibonacci：快速倍增更快，F(2k)=F(k)(2F(k+1)-F(k))、F(2k+1)=F(k)^2+F(k+1)^2，m < 2^31
static uint32_t fib_mod(uint64_t n, uint32_t m) {
    uint64_t a = 0, b = 1 % m;                    // F(0), F(1)
    if (n == 0) return 0;
    for (int i = 63 - __builtin_clzll(n); i >= 0; i--) {
        uint64_t c = a * ((2 * b + m - a) % m) % m;
        uint64_t d = (a * a + b * b) % m;
        if ((n >> i) & 1) { a = d; b = (c + d) % m; } else { a = c; b = d; }
    }
    return (uint32_t)a;
}
```

### 3.15 循環偵測、位元計數公式

```c
// 迭代 x→f(x) 的循環偵測（Brent，O(1) 記憶體）：前 mu 項不重複，之後週期 lam
// 「第 1e18 次迭代的值」「LCG 第 n 項」「Pisano 週期」都用這招
static uint32_t cyc_a = 1103515245u, cyc_c = 12345u, cyc_m = 1000003u;
static uint32_t cyc_f(uint32_t x) { return (uint32_t)(((uint64_t)cyc_a * x + cyc_c) % cyc_m); }   // ← 換成題目的 f
static void find_cycle(uint32_t x0, uint64_t *mu, uint64_t *lam) {
    uint64_t power = 1, l = 1, m = 0;
    uint32_t tort = x0, hare = cyc_f(x0);
    while (tort != hare) {
        if (power == l) { tort = hare; power <<= 1; l = 0; }
        hare = cyc_f(hare); l++;
    }
    tort = hare = x0;
    for (uint64_t i = 0; i < l; i++) hare = cyc_f(hare);
    while (tort != hare) { tort = cyc_f(tort); hare = cyc_f(hare); m++; }
    *mu = m; *lam = l;
}
static uint32_t iterate_n(uint32_t x0, uint64_t n) {          // f^n(x0)
    uint64_t mu, lam, steps;
    find_cycle(x0, &mu, &lam);
    steps = n < mu ? n : mu + (n - mu) % lam;
    while (steps--) x0 = cyc_f(x0);
    return x0;
}
```

```c
// 0..n 之中第 k 位元為 1 的個數，O(1)
static uint64_t count_bit_k(uint64_t n, int k) {
    uint64_t blk = 1ull << (k + 1), half = 1ull << k, c = (n + 1) / blk * half;
    uint64_t rem = (n + 1) % blk;
    return c + (rem > half ? rem - half : 0);
}
static uint64_t popcount_sum(uint64_t n) {       // Σ_{i=0..n} popcount(i)，O(log n)
    uint64_t s = 0;
    for (int k = 0; k < 63 && (n >> k); k++) s += count_bit_k(n, k);
    return s;
}
// Gosper：下一個 popcount 相同的較大整數（列舉 n 選 k 的所有遮罩）
static uint32_t next_comb(uint32_t x) {
    uint32_t c = x & (0u - x), r = x + c;
    return (((r ^ x) >> 2) / c) | r;
}
// 其他：x&-x 最低位；x&(x-1) 清最低位；!(x&(x-1)) 是否 2 的冪；
// 列舉 m 的所有非空子集：for (s = m; s; s = (s - 1) & m)
// SWAR popcount（沒有 cpop 指令時）
static uint32_t popcount_swar(uint32_t x) {
    x = x - ((x >> 1) & 0x55555555u);
    x = (x & 0x33333333u) + ((x >> 2) & 0x33333333u);
    x = (x + (x >> 4)) & 0x0F0F0F0Fu;
    return (x * 0x01010101u) >> 24;
}
```

### 3.16 公式化求和、整除分塊、排容

```c
// 公式取代迴圈：Σi = n(n+1)/2、Σi² = n(n+1)(2n+1)/6、Σi³ = (n(n+1)/2)²（先除再乘避免溢位）
static uint64_t sum1(uint64_t n) { return (n & 1) ? n * ((n + 1) / 2) : (n / 2) * (n + 1); }
// 等比 1 + r + ... + r^(n-1) mod m，任何 m 都行（不用反元素），O(log n)
static uint32_t geo_sum(uint32_t r, uint64_t n, uint32_t m) {
    uint64_t S = 0, P = 1 % m;                    // S = S(k), P = r^k，從 k=0 開始
    r %= m;
    for (int i = 63; i >= 0; i--) {
        S = S * (1 + P) % m; P = P * P % m;     // k → 2k
        if ((n >> i) & 1) { S = (S + P) % m; P = P * r % m; }   // k → k+1
    }
    return (uint32_t)S;
}
// Σ_{i=1..n} floor(n/i)，O(sqrt n)：floor(n/i) 只有約 2 sqrt(n) 種值，同值一段一起算
static uint64_t sum_floor_div(uint64_t n) {
    uint64_t s = 0;
    for (uint64_t i = 1, j; i <= n; i = j + 1) {
        uint64_t q = n / i;
        j = n / q;                                // 最後一個 floor(n/x) == q 的 x
        s += q * (j - i + 1);
    }
    return s;
}
// Σ_{i=1..n} σ(i)（因數和的總和）= Σ_d d*floor(n/d)，同樣分段，O(sqrt n)
static uint64_t sum_sigma(uint64_t n) {
    uint64_t s = 0;
    for (uint64_t i = 1, j; i <= n; i = j + 1) {
        uint64_t q = n / i;
        j = n / q;
        s += q * (sum1(j) - sum1(i - 1));
    }
    return s;
}
// n! 中質數 p 的次方數（Legendre）；n! 尾端 0 的個數 = legendre(n, 5)
static uint64_t legendre(uint64_t n, uint64_t p) { uint64_t e = 0; while (n) { n /= p; e += n; } return e; }
```

```c
static uint64_t gcd_u64(uint64_t a, uint64_t b) {
    if (!a) return b;
    if (!b) return a;
    int sh = __builtin_ctzll(a | b);
    a >>= __builtin_ctzll(a);
    do {
        b >>= __builtin_ctzll(b);
        if (a > b) { uint64_t t = a; a = b; b = t; }
        b -= a;
    } while (b);
    return a << sh;
}
// 排容：1..n 中「能被 a[0..k-1] 至少一個整除」的個數，O(2^k · k)。lcm 超過 n 那項是 0，直接跳過
static uint64_t count_divisible_any(uint64_t n, const uint32_t *a, int k) {
    int64_t total = 0;
    for (uint32_t s = 1; s < (1u << k); s++) {
        uint64_t l = 1; int bits = 0, over = 0;
        for (int i = 0; i < k && !over; i++)
            if ((s >> i) & 1) {
                uint64_t q = l / gcd_u64(l, a[i]);
                bits++;
                if (q > n / a[i]) over = 1; else l = q * a[i];
            }
        if (over) continue;
        total += (bits & 1) ? (int64_t)(n / l) : -(int64_t)(n / l);
    }
    return (uint64_t)total;
}
// 互質計數：1..n 中與 x 互質的個數 = n - count_divisible_any(n, x 的相異質因數, k)
```

### 3.17 64 位元質數判斷與 Pollard rho（需要 3.7、3.10 的 `mulhi64`、3.16 的 `gcd_u64`）

```c
// 64 位元：Montgomery 乘法（用 mulhi64，不需要 __int128）→ Miller-Rabin → Pollard rho（Brent 版）
typedef struct { uint64_t m, minv, one, r2; } mont64_t;
static mont64_t mont64_init(uint64_t m) {          // m 奇數 < 2^63
    mont64_t M; uint64_t inv = m, r;
    for (int i = 0; i < 5; i++) inv *= 2 - m * inv;
    M.m = m; M.minv = 0 - inv;
    M.one = (0 - m) % m;                            // R = 2^64 mod m
    r = M.one;
    for (int i = 0; i < 64; i++) r = (r >= m - r) ? r - (m - r) : r + r;   // R^2 mod m
    M.r2 = r;
    return M;
}
static inline uint64_t m64_mul(const mont64_t *M, uint64_t a, uint64_t b) {
    uint64_t lo = a * b, hi = mulhi64(a, b), u = lo * M->minv;
    uint64_t t = hi + mulhi64(u, M->m) + (lo != 0);
    return t >= M->m ? t - M->m : t;
}
static inline uint64_t m64_to(const mont64_t *M, uint64_t a) { return m64_mul(M, a % M->m, M->r2); }
static int is_prime_u64(uint64_t n) {
    static const uint64_t base[] = {2, 325, 9375, 28178, 450775, 9780504, 1795265022};  // 對所有 64 位元正確
    if (n < 4294967296ull) return is_prime_u32((uint32_t)n);
    if (!(n & 1)) return 0;
    mont64_t M = mont64_init(n);
    uint64_t d = n - 1, mone = n - M.one;          // mone = -1 的 Montgomery 形式
    int s = __builtin_ctzll(d);
    d >>= s;
    for (int i = 0; i < 7; i++) {
        uint64_t a = base[i] % n, x = M.one, b, e = d;
        if (!a) continue;
        b = m64_to(&M, a);
        while (e) { if (e & 1) x = m64_mul(&M, x, b); b = m64_mul(&M, b, b); e >>= 1; }
        if (x == M.one || x == mone) continue;
        int ok = 0;
        for (int r = 1; r < s && !ok; r++) { x = m64_mul(&M, x, x); ok = (x == mone); }
        if (!ok) return 0;
    }
    return 1;
}
static inline uint64_t rho_f(const mont64_t *M, uint64_t v, uint64_t cm) {   // v^2 + c
    uint64_t t = m64_mul(M, v, v) + cm;
    return t >= M->m ? t - M->m : t;
}
static uint64_t rho(uint64_t n, uint64_t c) {      // n 奇合數；回傳一個因數（可能等於 n，換 c 重試）
    mont64_t M = mont64_init(n);
    uint64_t cm = m64_to(&M, c), y = M.one, x = y, ys = y, q = M.one, g = 1;
    for (uint64_t r = 1; g == 1; r <<= 1) {
        x = y;
        for (uint64_t i = 0; i < r; i++) y = rho_f(&M, y, cm);
        for (uint64_t k = 0; k < r && g == 1; k += 128) {
            ys = y;
            for (uint64_t i = 0; i < 128 && i < r - k; i++) {
                y = rho_f(&M, y, cm);
                q = m64_mul(&M, q, x > y ? x - y : y - x);
            }
            g = gcd_u64(q, n);
        }
    }
    if (g == n)
        do { ys = rho_f(&M, ys, cm); g = gcd_u64(x > ys ? x - ys : ys - x, n); } while (g == 1);
    return g;
}
// 分解 n < 2^63：質因數（含重複、未排序）寫進 out，回傳個數（<= 63）。用陣列當堆疊，不遞迴
static int factor_u64(uint64_t n, uint64_t *out) {
    uint64_t st[64]; int k = 0, sp = 0;
    if (n < 2) return 0;
    while (!(n & 1)) { out[k++] = 2; n >>= 1; }
    for (uint64_t p = 3; p < 100 && p * p <= n; p += 2)
        while (n % p == 0) { out[k++] = p; n /= p; }
    if (n > 1) st[sp++] = n;
    while (sp) {
        uint64_t x = st[--sp], d = x;
        if (is_prime_u64(x)) { out[k++] = x; continue; }
        for (uint64_t c = 1; d == x; c++) d = rho(x, c);
        st[sp++] = d; st[sp++] = x / d;
    }
    return k;
}
```

### 3.18 離散對數 BSGS（需要 3.1、3.2）

```c
// 離散對數：找最小 x >= 0 使 a^x ≡ b (mod p)，p 質數，O(sqrt p)。找不到回傳 -1
// 用開放定址雜湊表存 baby steps：RAM = 8 * HT bytes
#define HT (1u << 16)                             // 要 >= 2*sqrt(p)，p < 2^31 → 2^16 夠用到 p≈1e9
static uint32_t ht_key[HT], ht_val[HT];
static uint8_t ht_used[HT];
static int64_t bsgs(uint32_t a, uint32_t b, uint32_t p) {
    uint32_t n = isqrt32(p) + 1;
    a %= p; b %= p;
    if (1 % p == b) return 0;
    memset(ht_used, 0, sizeof ht_used);
    uint64_t cur = b;                             // baby: b * a^j，j = 0..n-1
    for (uint32_t j = 0; j < n; j++) {
        uint32_t h = (uint32_t)(cur * 2654435761u) & (HT - 1);
        while (ht_used[h] && ht_key[h] != cur) h = (h + 1) & (HT - 1);
        ht_used[h] = 1; ht_key[h] = (uint32_t)cur; ht_val[h] = j;   // 同 key 留最大的 j
        cur = cur * a % p;
    }
    uint32_t an = pow_mod(a, n, p);
    uint64_t g = an;                              // giant: a^(n*i)，i = 1..n
    for (uint32_t i = 1; i <= n; i++) {
        uint32_t h = (uint32_t)(g * 2654435761u) & (HT - 1);
        while (ht_used[h]) {
            if (ht_key[h] == g) return (int64_t)i * n - ht_val[h];
            h = (h + 1) & (HT - 1);
        }
        g = g * an % p;
    }
    return -1;
}
```

### 3.19 數論儲備知識（小模型常不知道的）

| 定理 / 事實 | 內容 | 用途 |
|---|---|---|
| 費馬小定理 | p 質數、p∤a：a^(p−1) ≡ 1 (mod p) | 反元素 a^(p−2)；指數先 mod (p−1) |
| 歐拉定理 | gcd(a,m)=1：a^φ(m) ≡ 1 (mod m) | 超大指數：e 先 mod φ(m) |
| 擴展歐拉 | e ≥ φ(m) 時 a^e ≡ a^(e mod φ(m) + φ(m)) (mod m)，不需互質 | 冪塔 a^b^c mod m |
| Wilson | (p−1)! ≡ −1 (mod p) | 判質數（理論用） |
| gcd 性質 | gcd(a,b)=gcd(b, a mod b)=gcd(a−b, b)；gcd(F_m, F_n)=F_gcd(m,n)；gcd(2^a−1, 2^b−1)=2^gcd(a,b)−1 | 化簡 |
| 因數個數上限 | n ≤ 10⁹ 時最多 1344 個（735134400） | 估「枚舉因數」複雜度 |
| 質數密度 | π(N) ≈ N / ln N；π(10⁶)=78498、π(10⁸)=5761455、π(2³²)=203280221 | 估陣列大小 |
| 質數間隔 | 32 位元內相鄰質數間隔 < 340 | 「找下一個質數」只要試幾百個 |
| Σφ(d)（d|n）= n | | 數 gcd：Σ_{i≤n} gcd(i,n) = Σ_{d|n} d·φ(n/d) |
| Möbius 反演 | [gcd=1] = Σ_{d|gcd} μ(d) | 數互質對：Σ_d μ(d)·⌊n/d⌋² |
| Legendre | n! 中 p 的次方 = Σ ⌊n/p^k⌋ | 尾數 0 個數、C(n,r) 被 p 整除幾次 |
| Lucas | C(n,r) 為奇數 ⇔ (r & ~n) == 0 | 巴斯卡三角形奇偶，O(1) |
| Pisano 週期 | Fibonacci mod m 有週期，mod 10 是 60、mod 10^k 是 15·10^(k−1)（k≥3） | F(n) mod m 先 n mod 週期 |
| 卡特蘭數 | C_n = C(2n,n)/(n+1)：1,1,2,5,14,42,132,429,1430 | 括號、二元樹、不交叉路徑 |
| 錯排 | D_n = (n−1)(D_{n−1}+D_{n−2})：1,0,1,2,9,44,265,1854 | 「沒人拿到自己的」 |
| 貝爾數 | 1,1,2,5,15,52,203,877,4140 | 集合分割數 |
| 整數分割 | 1,1,2,3,5,7,11,15,22,30,42 | 把 n 拆成正整數和（不計順序） |
| Motzkin | 1,1,2,4,9,21,51,127,323 | 圓上不交叉弦 |
| 星與槓 | n 個相同球放 k 盒：C(n+k−1, k−1) | 計數 |
| 格點路徑 | (0,0)→(a,b) 只往右上：C(a+b, a) | DP 化成公式 |
| 二項式反轉、Burnside | 項鍊數 = (1/n) Σ_{d|n} φ(d)·k^(n/d) | 旋轉同構計數 |

（數列由 Python 直接計算核對；定理為標準結果，未逐一在題目中驗證。）

---

## 4. 排序與選擇

### 4.1 基數排序（取代 qsort）

```c
// LSD 基數排序 uint32：4 輪 × 8 位元，O(4n)，穩定。tmp 要 n 個空間。n 大於約 64 就比 qsort 快很多
// 有號 int32：排序前 x ^= 0x80000000u，排完再 XOR 回來
static void radix_sort_u32(uint32_t *a, uint32_t *tmp, uint32_t n) {
    uint32_t *src = a, *dst = tmp;
    for (int sh = 0; sh < 32; sh += 8) {
        uint32_t cnt[256] = {0}, sum = 0;
        for (uint32_t i = 0; i < n; i++) cnt[(src[i] >> sh) & 255]++;
        if (cnt[(src[0] >> sh) & 255] == n) continue;     // 這一輪全部同一桶：跳過
        for (int b = 0; b < 256; b++) { uint32_t c = cnt[b]; cnt[b] = sum; sum += c; }
        for (uint32_t i = 0; i < n; i++) dst[cnt[(src[i] >> sh) & 255]++] = src[i];
        uint32_t *t = src; src = dst; dst = t;
    }
    if (src != a) memcpy(a, src, n * sizeof *a);
}
// 依 key[] 排序「索引」idx[0..n-1]（例如依邊權排序邊），穩定
static void radix_sort_idx(uint32_t *idx, uint32_t *tmp, const uint32_t *key, uint32_t n) {
    uint32_t *src = idx, *dst = tmp;
    for (int sh = 0; sh < 32; sh += 8) {
        uint32_t cnt[256] = {0}, sum = 0;
        for (uint32_t i = 0; i < n; i++) cnt[(key[src[i]] >> sh) & 255]++;
        for (int b = 0; b < 256; b++) { uint32_t c = cnt[b]; cnt[b] = sum; sum += c; }
        for (uint32_t i = 0; i < n; i++) dst[cnt[(key[src[i]] >> sh) & 255]++] = src[i];
        uint32_t *t = src; src = dst; dst = t;
    }
    if (src != idx) memcpy(idx, src, n * sizeof *idx);
}
```

### 4.2 計數排序、插入排序

```c
// 值域小（0..R-1）：計數排序 O(n + R)，不需要額外的 n 空間
static void counting_sort(uint32_t *a, uint32_t n, uint32_t *cnt, uint32_t R) {
    memset(cnt, 0, R * sizeof *cnt);
    for (uint32_t i = 0; i < n; i++) cnt[a[i]]++;
    for (uint32_t v = 0, k = 0; v < R; v++)
        for (uint32_t c = cnt[v]; c; c--) a[k++] = v;
}
// n <= 16 左右：插入排序最快（也拿來收尾）
static void insertion_sort(uint32_t *a, uint32_t n) {
    for (uint32_t i = 1; i < n; i++) {
        uint32_t x = a[i], j = i;
        while (j && a[j - 1] > x) { a[j] = a[j - 1]; j--; }
        a[j] = x;
    }
}
```

### 4.3 Quickselect（第 k 小）

```c
// 第 k 小（0 起算），平均 O(n)，會打亂陣列。中位數、第 k 大、前 k 小都用它，不用整個排序
static uint32_t quickselect(uint32_t *a, uint32_t n, uint32_t k) {
    uint32_t lo = 0, hi = n - 1;
    while (hi > lo) {
        uint32_t mid = lo + (hi - lo) / 2, t;                  // 三數取中當 pivot
        if (a[mid] < a[lo]) { t = a[mid]; a[mid] = a[lo]; a[lo] = t; }
        if (a[hi] < a[lo]) { t = a[hi]; a[hi] = a[lo]; a[lo] = t; }
        if (a[hi] < a[mid]) { t = a[hi]; a[hi] = a[mid]; a[mid] = t; }
        uint32_t p = a[mid], i = lo, j = hi;
        while (i <= j) {                                        // Hoare 分割
            while (a[i] < p) i++;
            while (a[j] > p) j--;
            if (i <= j) { t = a[i]; a[i] = a[j]; a[j] = t; i++; if (j == 0) break; j--; }
        }
        if (k <= j) hi = j;
        else if (k >= i) lo = i;
        else return a[k];
    }
    return a[k];
}
```

---

## 5. 圖論範本

共用：5.1 的 `head/adj/wt`、`INF`；5.2 的 `dist/que`；5.3 的 `stk/it_`。不用的陣列刪掉省 RAM。

### 5.1 CSR 建圖

```c
// CSR（壓縮鄰接表）：沒有 malloc、沒有指標，u 的鄰居是 adj[head[u] .. head[u+1]-1]
// RAM = 4*(N + 2M) bytes（含權重）。無向圖每條邊放兩次，M 要算兩倍
#define MAXN 20000
#define MAXM 200000
#define INF 0xFFFFFFFFu
static uint32_t head[MAXN + 1], adj[MAXM], wt[MAXM];
static void csr_build(uint32_t n, uint32_t m, const uint32_t *eu, const uint32_t *ev, const uint32_t *ew) {
    static uint32_t pos[MAXN];
    memset(head, 0, (n + 1) * sizeof head[0]);
    for (uint32_t i = 0; i < m; i++) head[eu[i] + 1]++;
    for (uint32_t i = 0; i < n; i++) head[i + 1] += head[i];
    memcpy(pos, head, n * sizeof pos[0]);
    for (uint32_t i = 0; i < m; i++) {
        uint32_t k = pos[eu[i]]++;
        adj[k] = ev[i];
        if (ew) wt[k] = ew[i];
    }
}
```

### 5.2 BFS（需要 5.1）

```c
// 無權最短路 / 可達性，O(N+M)，佇列就是陣列
static uint32_t dist[MAXN], que[MAXN];
static void bfs(uint32_t n, uint32_t s) {
    uint32_t qh = 0, qt = 0;
    memset(dist, 0xFF, n * sizeof dist[0]);
    dist[s] = 0; que[qt++] = s;
    while (qh < qt) {
        uint32_t u = que[qh++];
        for (uint32_t k = head[u]; k < head[u + 1]; k++) {
            uint32_t v = adj[k];
            if (dist[v] == INF) { dist[v] = dist[u] + 1; que[qt++] = v; }
        }
    }
}
```

### 5.3 迭代 DFS：找環、後序（需要 5.1）

```c
// 迭代 DFS（不遞迴，MCU 堆疊只有幾 KB）：有向圖找環 + 後序。order 反過來就是拓樸序
static uint8_t color[MAXN];                  // 0 沒走過、1 在堆疊上、2 完成
static uint32_t stk[MAXN], it_[MAXN];
static int dfs_has_cycle(uint32_t n, uint32_t *order) {
    uint32_t cnt = 0;
    memset(color, 0, n);
    for (uint32_t s = 0; s < n; s++) {
        if (color[s]) continue;
        uint32_t sp = 0;
        stk[sp++] = s; color[s] = 1; it_[s] = head[s];
        while (sp) {
            uint32_t u = stk[sp - 1];
            if (it_[u] < head[u + 1]) {
                uint32_t v = adj[it_[u]++];
                if (color[v] == 1) return 1;            // 回邊 → 有環
                if (!color[v]) { color[v] = 1; it_[v] = head[v]; stk[sp++] = v; }
            } else {
                color[u] = 2; sp--;
                if (order) order[cnt++] = u;
            }
        }
    }
    return 0;
}
```

### 5.4 Kahn 拓樸排序（需要 5.1）

```c
// Kahn 拓樸排序 O(N+M)。回傳排進 out 的個數；< n 代表有環（DAG 判定）
static uint32_t indeg[MAXN];
static uint32_t topo_kahn(uint32_t n, uint32_t *out) {
    uint32_t h = 0, t = 0;
    memset(indeg, 0, n * sizeof indeg[0]);
    for (uint32_t k = 0; k < head[n]; k++) indeg[adj[k]]++;
    for (uint32_t u = 0; u < n; u++) if (!indeg[u]) out[t++] = u;
    while (h < t) {                               // out 本身就是佇列
        uint32_t u = out[h++];
        for (uint32_t k = head[u]; k < head[u + 1]; k++)
            if (--indeg[adj[k]] == 0) out[t++] = adj[k];
    }
    return t;
}
// DAG 上的最長/最短路、路徑數：照 out 的順序做一次 DP 就好，O(N+M)
```

### 5.5 Dijkstra（需要 5.1、5.2 的 `dist`）

```c
// Dijkstra + 二元堆積，O((N+M) log M)。(距離<<32 | 節點) 打包成一個 uint64：比較只要一次
// 權重非負、距離總和 < 2^32。延遲刪除：彈出的距離比 dist 舊就跳過
static uint64_t heap[MAXM + 1];
static uint32_t hn;
static void hpush(uint64_t x) {
    uint32_t i = hn++;
    while (i) { uint32_t p = (i - 1) >> 1; if (heap[p] <= x) break; heap[i] = heap[p]; i = p; }
    heap[i] = x;
}
static uint64_t hpop(void) {
    uint64_t top = heap[0], x = heap[--hn];
    uint32_t i = 0;
    for (;;) {
        uint32_t c = 2 * i + 1;
        if (c >= hn) break;
        if (c + 1 < hn && heap[c + 1] < heap[c]) c++;
        if (x <= heap[c]) break;
        heap[i] = heap[c]; i = c;
    }
    heap[i] = x;
    return top;
}
static void dijkstra(uint32_t n, uint32_t s) {
    memset(dist, 0xFF, n * sizeof dist[0]);
    dist[s] = 0; hn = 0; hpush(s);
    while (hn) {
        uint64_t top = hpop();
        uint32_t u = (uint32_t)top, d = (uint32_t)(top >> 32);
        if (d != dist[u]) continue;
        for (uint32_t k = head[u]; k < head[u + 1]; k++) {
            uint32_t v = adj[k], nd = d + wt[k];
            if (nd < dist[v]) { dist[v] = nd; hpush((uint64_t)nd << 32 | v); }
        }
    }
}
```

### 5.6 0-1 BFS（需要 5.1、5.2 的 `dist`）

```c
// 0-1 BFS：權重只有 0 或 1，用雙端佇列，O(N+M)，比 Dijkstra 少一個 log
#define DQ (1u << 18)                             // 2 的冪，>= M+1
static uint32_t dq[DQ];
static uint8_t done01[MAXN];
static void bfs01(uint32_t n, uint32_t s) {
    uint32_t h = 0, t = 0;                        // 無號數自然溢位，用 & (DQ-1) 取位置
    memset(dist, 0xFF, n * sizeof dist[0]);
    memset(done01, 0, n);
    dist[s] = 0; dq[t++ & (DQ - 1)] = s;
    while (h != t) {
        uint32_t u = dq[h++ & (DQ - 1)];
        if (done01[u]) continue;
        done01[u] = 1;
        for (uint32_t k = head[u]; k < head[u + 1]; k++) {
            uint32_t v = adj[k], nd = dist[u] + wt[k];
            if (nd < dist[v]) {
                dist[v] = nd;
                if (wt[k]) dq[t++ & (DQ - 1)] = v; else dq[--h & (DQ - 1)] = v;
            }
        }
    }
}
```

### 5.7 Bellman–Ford（邊列表，不需要 CSR）

```c
// Bellman-Ford（可有負權）O(NM)，一輪沒變就提早結束。回傳 1 = 從 s 可達負環
static int64_t bdist[MAXN];
static int bellman_ford(uint32_t n, uint32_t m, const uint32_t *eu, const uint32_t *ev, const int32_t *ew, uint32_t s) {
    for (uint32_t i = 0; i < n; i++) bdist[i] = INT64_MAX;
    bdist[s] = 0;
    for (uint32_t round = 0; round < n; round++) {
        int changed = 0;
        for (uint32_t i = 0; i < m; i++)
            if (bdist[eu[i]] != INT64_MAX && bdist[eu[i]] + ew[i] < bdist[ev[i]]) {
                bdist[ev[i]] = bdist[eu[i]] + ew[i]; changed = 1;
            }
        if (!changed) return 0;
    }
    return 1;                                     // 第 n 輪還在變
}
```

### 5.8 Floyd–Warshall

```c
// Floyd–Warshall 全點對 O(N^3)。N=200 → 160 KB。INF 取 0x3FFFFFFF 讓兩個相加不溢位
#define FW 200
#define FINF 0x3FFFFFFFu
static uint32_t fw[FW][FW];
static void floyd(uint32_t n) {                   // 先設 fw[i][i]=0、邊權、其餘 FINF
    for (uint32_t k = 0; k < n; k++)
        for (uint32_t i = 0; i < n; i++) {
            uint32_t dik = fw[i][k];
            if (dik == FINF) continue;            // 這一列整列跳過
            for (uint32_t j = 0; j < n; j++) {
                uint32_t t = dik + fw[k][j];
                if (t < fw[i][j]) fw[i][j] = t;
            }
        }
}
```

### 5.9 位元集合遞移閉包

```c
// 遞移閉包（誰能到誰）用位元集合：O(N^3/32)，比 Floyd 快 32 倍。N=1024 → 128 KB
#define BN 1024
#define BW (BN / 32)
static uint32_t reach[BN][BW];                    // 先放直接邊；要「自己到自己」就設 bit i
static void closure(uint32_t n) {
    uint32_t w = (n + 31) >> 5;
    for (uint32_t k = 0; k < n; k++)
        for (uint32_t i = 0; i < n; i++)
            if ((reach[i][k >> 5] >> (k & 31)) & 1)
                for (uint32_t j = 0; j < w; j++) reach[i][j] |= reach[k][j];
}
```

### 5.10 位元集合 BFS（需要 5.2 的 `dist/que`、5.9 的 `BN/BW`）

```c
// 稠密圖 BFS：鄰接矩陣用位元，一次處理 32 個鄰居，O(N^2/32)
static uint32_t abit[BN][BW], visb[BW];
static void bfs_bitset(uint32_t n, uint32_t s) {
    uint32_t w = (n + 31) >> 5, qh = 0, qt = 0;
    memset(visb, 0, sizeof visb);
    memset(dist, 0xFF, n * sizeof dist[0]);
    dist[s] = 0; que[qt++] = s; visb[s >> 5] |= 1u << (s & 31);
    while (qh < qt) {
        uint32_t u = que[qh++];
        for (uint32_t j = 0; j < w; j++) {
            uint32_t nb = abit[u][j] & ~visb[j];   // 還沒走過的鄰居
            visb[j] |= nb;
            while (nb) {
                uint32_t v = (j << 5) + (uint32_t)__builtin_ctz(nb);
                nb &= nb - 1;
                dist[v] = dist[u] + 1; que[qt++] = v;
            }
        }
    }
}
```

### 5.11 並查集

```c
// 並查集：路徑減半 + 依大小合併，近乎 O(1)。動態連通、合併群組、無向圖找環、Kruskal 都用它
static uint32_t par[MAXN], sz[MAXN];
static void dsu_init(uint32_t n) { for (uint32_t i = 0; i < n; i++) { par[i] = i; sz[i] = 1; } }
static uint32_t dsu_find(uint32_t x) {
    while (par[x] != x) { par[x] = par[par[x]]; x = par[x]; }
    return x;
}
static int dsu_union(uint32_t a, uint32_t b) {    // 回傳 0 = 本來就同一群（無向圖：這條邊造成環）
    a = dsu_find(a); b = dsu_find(b);
    if (a == b) return 0;
    if (sz[a] < sz[b]) { uint32_t t = a; a = b; b = t; }
    par[b] = a; sz[a] += sz[b];
    return 1;
}
```

### 5.12 Kruskal（需要 4.1 的 `radix_sort_idx`、5.11）

```c
// Kruskal MST：基數排序邊（不用 qsort）+ 並查集，O(M α)。不連通回傳 UINT64_MAX。需要 radix_sort_idx
static uint64_t kruskal(uint32_t n, uint32_t m, const uint32_t *eu, const uint32_t *ev,
                        const uint32_t *ew, uint32_t *idx, uint32_t *tmp) {
    uint64_t total = 0;
    uint32_t used = 0;
    for (uint32_t i = 0; i < m; i++) idx[i] = i;
    radix_sort_idx(idx, tmp, ew, m);
    dsu_init(n);
    for (uint32_t i = 0; i < m && used + 1 < n; i++) {
        uint32_t e = idx[i];
        if (dsu_union(eu[e], ev[e])) { total += ew[e]; used++; }
    }
    return (n <= 1 || used + 1 == n) ? total : UINT64_MAX;
}
```

### 5.13 稠密圖 Dijkstra / Prim（鄰接矩陣，O(N²)）

```c
// 稠密圖（M ≈ N^2）：不用堆積，O(N^2) 的 Dijkstra / Prim 反而比較快。N=300 → 360 KB
#define DN 300
static uint32_t W[DN][DN];                        // INF = 沒有邊
static uint8_t used_[DN];
static void dijkstra_dense(uint32_t n, uint32_t s, uint32_t *d) {
    memset(used_, 0, n);
    for (uint32_t i = 0; i < n; i++) d[i] = INF;
    d[s] = 0;
    for (uint32_t it = 0; it < n; it++) {
        uint32_t u = INF, best = INF;
        for (uint32_t i = 0; i < n; i++) if (!used_[i] && d[i] < best) { best = d[i]; u = i; }
        if (u == INF) break;
        used_[u] = 1;
        for (uint32_t v = 0; v < n; v++)
            if (W[u][v] != INF && best + W[u][v] < d[v]) d[v] = best + W[u][v];
    }
}
static uint64_t prim_dense(uint32_t n, uint32_t *key) {      // 無向，W 對稱；不連通回傳 UINT64_MAX
    uint64_t total = 0;
    memset(used_, 0, n);
    for (uint32_t i = 0; i < n; i++) key[i] = INF;
    key[0] = 0;
    for (uint32_t it = 0; it < n; it++) {
        uint32_t u = INF, best = INF;
        for (uint32_t i = 0; i < n; i++) if (!used_[i] && key[i] < best) { best = key[i]; u = i; }
        if (u == INF) return UINT64_MAX;
        used_[u] = 1; total += best;
        for (uint32_t v = 0; v < n; v++) if (!used_[v] && W[u][v] < key[v]) key[v] = W[u][v];
    }
    return total;
}
```

### 5.14 二分圖判定（需要 5.1、5.2 的 `que`）

```c
// 二分圖判定（無向）：BFS 著色，O(N+M)。side[] 得到兩邊
static int8_t side[MAXN];
static int is_bipartite(uint32_t n) {
    memset(side, -1, n);
    for (uint32_t s = 0; s < n; s++) {
        if (side[s] >= 0) continue;
        uint32_t qh = 0, qt = 0;
        side[s] = 0; que[qt++] = s;
        while (qh < qt) {
            uint32_t u = que[qh++];
            for (uint32_t k = head[u]; k < head[u + 1]; k++) {
                uint32_t v = adj[k];
                if (side[v] < 0) { side[v] = (int8_t)(side[u] ^ 1); que[qt++] = v; }
                else if (side[v] == side[u]) return 0;   // 奇環
            }
        }
    }
    return 1;
}
```

### 5.15 強連通分量 Tarjan、2-SAT（需要 5.1、5.3 的 `stk/it_`）

```c
// 強連通分量（Tarjan，迭代版）O(N+M)。comp[v] = 分量編號；編號是縮點後 DAG 的「反」拓樸序
static uint32_t tin[MAXN], low[MAXN], comp[MAXN], tstk[MAXN];
static uint8_t onst[MAXN];
static uint32_t scc_tarjan(uint32_t n) {
    uint32_t timer = 0, ncomp = 0, tsp = 0;
    memset(tin, 0xFF, n * sizeof tin[0]);
    memset(onst, 0, n);
    for (uint32_t s = 0; s < n; s++) {
        if (tin[s] != INF) continue;
        uint32_t sp = 0;
        stk[sp++] = s; tin[s] = low[s] = timer++; tstk[tsp++] = s; onst[s] = 1; it_[s] = head[s];
        while (sp) {
            uint32_t u = stk[sp - 1];
            if (it_[u] < head[u + 1]) {
                uint32_t v = adj[it_[u]++];
                if (tin[v] == INF) {
                    tin[v] = low[v] = timer++; tstk[tsp++] = v; onst[v] = 1; it_[v] = head[v]; stk[sp++] = v;
                } else if (onst[v] && tin[v] < low[u]) low[u] = tin[v];
            } else {
                sp--;
                if (sp && low[u] < low[stk[sp - 1]]) low[stk[sp - 1]] = low[u];
                if (low[u] == tin[u]) {
                    uint32_t w;
                    do { w = tstk[--tsp]; onst[w] = 0; comp[w] = ncomp; } while (w != u);
                    ncomp++;
                }
            }
        }
    }
    return ncomp;
}
// 2-SAT：變數 x 用節點 2x（真）、2x+1（假）；子句 (a∨b) 加邊 ¬a→b、¬b→a。
// 有解 ⇔ 沒有 comp[2x]==comp[2x+1]；x 取真 ⇔ comp[2x] < comp[2x+1]
```

### 5.16 橋與割點（需要 5.1、5.3、5.15 的 `tin/low`）

```c
// 橋與割點（無向，迭代版）O(N+M)。建 CSR 時把 wt[k] 設成「原始邊編號」，重邊也正確
static uint32_t pe[MAXN];                         // 走進這個點用的邊編號
static uint8_t is_art[MAXN], is_bridge[MAXM / 2];
static void bridges_arts(uint32_t n, uint32_t m_undirected) {
    uint32_t timer = 0;
    memset(tin, 0xFF, n * sizeof tin[0]);
    memset(is_art, 0, n);
    memset(is_bridge, 0, m_undirected);
    for (uint32_t s = 0; s < n; s++) {
        if (tin[s] != INF) continue;
        uint32_t sp = 0, rootkids = 0;
        stk[sp++] = s; tin[s] = low[s] = timer++; pe[s] = INF; it_[s] = head[s];
        while (sp) {
            uint32_t u = stk[sp - 1];
            if (it_[u] < head[u + 1]) {
                uint32_t k = it_[u]++, v = adj[k], e = wt[k];
                if (e == pe[u]) continue;         // 不走回同一條邊
                if (tin[v] == INF) {
                    tin[v] = low[v] = timer++; pe[v] = e; it_[v] = head[v]; stk[sp++] = v;
                    if (u == s) rootkids++;
                } else if (tin[v] < low[u]) low[u] = tin[v];
            } else {
                sp--;
                if (sp) {
                    uint32_t p = stk[sp - 1];
                    if (low[u] < low[p]) low[p] = low[u];
                    if (low[u] > tin[p]) is_bridge[pe[u]] = 1;
                    if (p != s && low[u] >= tin[p]) is_art[p] = 1;
                }
            }
        }
        if (rootkids >= 2) is_art[s] = 1;
    }
}
```

### 5.17 LCA（跳躍指標，需要 5.1、5.2 的 `que`）

```c
// LCA：跳躍指標（jump pointer）版，O(N) 記憶體、每次查詢 O(log N)
// 二進位提升要 N*logN 個整數（N=1e5 就 6.8 MB），MCU 放不下；這個只要 3 個陣列
static uint32_t tpar[MAXN], jmp[MAXN], dep[MAXN];
static void lca_build(uint32_t n, uint32_t root) {   // 樹存成無向 CSR
    uint32_t qh = 0, qt = 0;
    memset(dep, 0xFF, n * sizeof dep[0]);
    dep[root] = 0; tpar[root] = jmp[root] = root; que[qt++] = root;
    while (qh < qt) {                                 // BFS：父節點一定先處理
        uint32_t u = que[qh++];
        for (uint32_t k = head[u]; k < head[u + 1]; k++) {
            uint32_t v = adj[k];
            if (dep[v] != INF) continue;
            dep[v] = dep[u] + 1; tpar[v] = u;
            uint32_t j = jmp[u];
            jmp[v] = (dep[u] - dep[j] == dep[j] - dep[jmp[j]]) ? jmp[j] : u;
            que[qt++] = v;
        }
    }
}
static uint32_t lca_up(uint32_t v, uint32_t d) {      // v 往上到深度 d 的祖先
    while (dep[v] > d) v = (dep[jmp[v]] >= d) ? jmp[v] : tpar[v];
    return v;
}
static uint32_t lca(uint32_t u, uint32_t v) {
    if (dep[u] > dep[v]) u = lca_up(u, dep[v]); else v = lca_up(v, dep[u]);
    while (u != v) {
        if (jmp[u] != jmp[v]) { u = jmp[u]; v = jmp[v]; }
        else { u = tpar[u]; v = tpar[v]; }
    }
    return u;
}
// 樹上兩點距離 = dep[u] + dep[v] - 2*dep[lca(u,v)]
```

### 5.18 最大流 Dinic、二分匹配、最小割

```c
// 最大流 Dinic O(V^2 E)，單位容量（二分匹配）時是 O(E sqrt V) = Hopcroft–Karp
// 迭代版 DFS（沒有遞迴）。邊 e 與 e^1 互為反向邊
#define FN_ 2048
#define FM_ 40000                                  // 邊數 × 2
#define NIL 0xFFFFFFFFu
static uint32_t fh[FN_], fnx[FM_], fto[FM_], fcap[FM_], fit[FN_], flv[FN_], fq[FN_], fpath[FN_], fne;
static void flow_init(uint32_t n) { memset(fh, 0xFF, n * sizeof fh[0]); fne = 0; }
static void flow_add(uint32_t u, uint32_t v, uint32_t c) {
    fto[fne] = v; fcap[fne] = c; fnx[fne] = fh[u]; fh[u] = fne++;
    fto[fne] = u; fcap[fne] = 0; fnx[fne] = fh[v]; fh[v] = fne++;
}
static uint64_t dinic(uint32_t n, uint32_t s, uint32_t t) {
    uint64_t flow = 0;
    for (;;) {
        uint32_t qh = 0, qt = 0;                     // BFS 分層
        memset(flv, 0xFF, n * sizeof flv[0]);
        flv[s] = 0; fq[qt++] = s;
        while (qh < qt) {
            uint32_t u = fq[qh++];
            for (uint32_t e = fh[u]; e != NIL; e = fnx[e])
                if (fcap[e] && flv[fto[e]] == INF) { flv[fto[e]] = flv[u] + 1; fq[qt++] = fto[e]; }
        }
        if (flv[t] == INF) return flow;
        memcpy(fit, fh, n * sizeof fh[0]);
        for (;;) {                                   // 一次找一條增廣路
            uint32_t u = s, plen = 0;
            while (u != t) {
                uint32_t e = fit[u];
                while (e != NIL && !(fcap[e] && flv[fto[e]] == flv[u] + 1)) e = fnx[e];
                fit[u] = e;
                if (e == NIL) {                      // 死路：標記並退一步
                    flv[u] = INF;
                    if (!plen) break;
                    u = fto[fpath[--plen] ^ 1];
                } else { fpath[plen++] = e; u = fto[e]; }
            }
            if (u != t) break;
            uint32_t f = INF;
            for (uint32_t i = 0; i < plen; i++) if (fcap[fpath[i]] < f) f = fcap[fpath[i]];
            for (uint32_t i = 0; i < plen; i++) { fcap[fpath[i]] -= f; fcap[fpath[i] ^ 1] += f; }
            flow += f;
        }
    }
}
// 二分匹配：s→左邊每點 cap 1，左→右 cap 1，右邊每點→t cap 1，答案 = dinic(s,t)
// 最小割 = 最大流；最後一次 BFS 中 flv[v] != INF 的點就是割的 s 那一側
```

### 5.19 方格 BFS

```c
// 方格最短路：外圈補一圈牆，就不用檢查邊界；四個方向 = 索引 ±1、±stride
// 不存距離陣列，一層一層 BFS 數層數 → RAM 只要 1 byte/格 + 佇列 2 bytes/格（uint16 索引，(H+2)(W+2) <= 65536）
#define GMAX 65536u
static uint8_t gwall[GMAX];                       // 1 = 牆或已走過（會被改掉，重跑要重設）
static uint16_t gq[GMAX];
static uint32_t grid_bfs(uint32_t H, uint32_t W, uint32_t sr, uint32_t sc, uint32_t tr, uint32_t tc) {
    int32_t S = (int32_t)W + 2;                   // 格子 (r,c) 在 (r+1)*S + (c+1)
    const int32_t dir[4] = {1, -1, S, -S};
    uint32_t s = (sr + 1) * S + sc + 1, t = (tr + 1) * S + tc + 1, qh = 0, qt = 0, layer = 0;
    (void)H;
    if (gwall[s]) return INF;
    gwall[s] = 1; gq[qt++] = (uint16_t)s;
    while (qh < qt) {
        uint32_t end = qt;                        // 這一層到 end 為止
        for (; qh < end; qh++) {
            uint32_t u = gq[qh];
            if (u == t) return layer;
            for (int d = 0; d < 4; d++) {
                uint32_t v = (uint32_t)((int32_t)u + dir[d]);
                if (!gwall[v]) { gwall[v] = 1; gq[qt++] = (uint16_t)v; }
            }
        }
        layer++;
    }
    return INF;
}
```

### 5.20 方格 A*（需要 5.5 的堆積、5.19）

實測結論：單位權方格上，BFS 每格只做幾個指令，A* 每格要進出堆積。終點在地圖另一角時，A* 展開約一半格子、反而**慢 4 倍**；終點離起點很近（距離 50、地圖 250×250）時 A* 快 2.1 倍。不確定就用 BFS。

```c
// A*（方格，曼哈頓距離當估計）：只往終點方向展開，空曠地圖展開的格子遠少於 BFS
// 需要上面的 heap/hpush/hpop 與 gwall；g 存已知距離
static uint32_t gdist[GMAX];
static uint32_t astar_expanded;
static uint32_t grid_astar(uint32_t H, uint32_t W, uint32_t sr, uint32_t sc, uint32_t tr, uint32_t tc) {
    int32_t S = (int32_t)W + 2;
    const int32_t dir[4] = {1, -1, S, -S};
    uint32_t s = (sr + 1) * S + sc + 1, t = (tr + 1) * S + tc + 1;
    memset(gdist, 0xFF, (H + 2) * (uint32_t)S * sizeof gdist[0]);
    astar_expanded = 0;
    if (gwall[s]) return INF;
    gdist[s] = 0; hn = 0;
    hpush((uint64_t)((sr > tr ? sr - tr : tr - sr) + (sc > tc ? sc - tc : tc - sc)) << 32 | s);
    while (hn) {
        uint32_t u = (uint32_t)hpop();
        if (u == t) return gdist[t];
        if (gwall[u] == 2) continue;              // 已關閉
        gwall[u] = 2; astar_expanded++;
        uint32_t ur = u / S, uc = u % S;          // 只在展開時算一次
        for (int d = 0; d < 4; d++) {
            uint32_t v = (uint32_t)((int32_t)u + dir[d]);
            if (gwall[v]) continue;
            uint32_t g = gdist[u] + 1;
            if (g < gdist[v]) {
                gdist[v] = g;
                uint32_t vr = ur + (d == 2) - (d == 3), vc = uc + (d == 0) - (d == 1);
                uint32_t h = (vr > tr + 1 ? vr - tr - 1 : tr + 1 - vr) + (vc > tc + 1 ? vc - tc - 1 : tc + 1 - vc);
                hpush((uint64_t)(g + h) << 32 | v);
            }
        }
    }
    return INF;
}
```

---

## 6. 區間查詢與資料結構

### 6.1 前綴和 / 2D 前綴和

```c
// 前綴和：P[i] = a[0]+..+a[i-1]，區間和 sum(l..r) = P[r+1]-P[l]，每次查詢 O(1)。用 int64 防溢位
static void prefix_build(const int32_t *a, uint32_t n, int64_t *P) {
    P[0] = 0;
    for (uint32_t i = 0; i < n; i++) P[i + 1] = P[i] + a[i];
}
// 2D：P 大小 (H+1)*(W+1)；矩形 (r1,c1)~(r2,c2) 含端點 = 四個角相加減
static void prefix2d_build(const int32_t *a, uint32_t H, uint32_t W, int64_t *P) {
    memset(P, 0, (W + 1) * sizeof *P);
    for (uint32_t r = 0; r < H; r++) {
        int64_t row = 0;
        P[(r + 1) * (W + 1)] = 0;
        for (uint32_t c = 0; c < W; c++) {
            row += a[r * W + c];
            P[(r + 1) * (W + 1) + c + 1] = P[r * (W + 1) + c + 1] + row;
        }
    }
}
static int64_t rect_sum(const int64_t *P, uint32_t W, uint32_t r1, uint32_t c1, uint32_t r2, uint32_t c2) {
    uint32_t w = W + 1;
    return P[(r2 + 1) * w + c2 + 1] - P[r1 * w + c2 + 1] - P[(r2 + 1) * w + c1] + P[r1 * w + c1];
}
```

### 6.2 差分陣列 / 2D 差分

```c
// 差分陣列：很多次「區間 [l,r] 全部加 v」最後才要結果 → 每次 O(1)，最後一次前綴和 O(n)
// D 大小 n+1，先清零
static inline void diff_add(int64_t *D, uint32_t l, uint32_t r, int64_t v) { D[l] += v; D[r + 1] -= v; }
static void diff_apply(const int64_t *D, uint32_t n, int64_t *a) {     // a[i] += 累積
    int64_t run = 0;
    for (uint32_t i = 0; i < n; i++) { run += D[i]; a[i] += run; }
}
// 2D 差分：矩形加 v → 四個角，最後做 2D 前綴和。D 大小 (H+1)*(W+1)
static inline void diff2d_add(int64_t *D, uint32_t W, uint32_t r1, uint32_t c1, uint32_t r2, uint32_t c2, int64_t v) {
    uint32_t w = W + 1;
    D[r1 * w + c1] += v; D[r1 * w + c2 + 1] -= v; D[(r2 + 1) * w + c1] -= v; D[(r2 + 1) * w + c2 + 1] += v;
}
static void diff2d_apply(int64_t *D, uint32_t H, uint32_t W) {         // 原地變成每格的增量
    uint32_t w = W + 1;
    for (uint32_t r = 0; r < H; r++)
        for (uint32_t c = 0; c < W; c++)
            D[r * w + c] += (c ? D[r * w + c - 1] : 0) + (r ? D[(r - 1) * w + c] : 0) - (r && c ? D[(r - 1) * w + c - 1] : 0);
}
```

### 6.3 Fenwick 樹（BIT）

```c
// Fenwick（BIT）：單點加值 + 前綴和，兩個都 O(log n)，陣列 n+1 個，索引從 1 開始
// 「邊改邊問」的區間和、逆序數對、第 k 小（值域 BIT）都用它
static void bit_add(int64_t *T, uint32_t n, uint32_t i, int64_t v) { for (; i <= n; i += i & (0u - i)) T[i] += v; }
static int64_t bit_sum(const int64_t *T, uint32_t i) { int64_t s = 0; for (; i; i -= i & (0u - i)) s += T[i]; return s; }  // a[1..i]
// 值域 BIT 上找「前綴和 >= k 的最小位置」（第 k 小），O(log n)，元素非負
static uint32_t bit_lower_bound(const int64_t *T, uint32_t n, int64_t k) {
    uint32_t pos = 0;
    for (uint32_t step = 1u << (31 - __builtin_clz(n)); step; step >>= 1)
        if (pos + step <= n && T[pos + step] < k) { pos += step; k -= T[pos]; }
    return pos + 1;
}
// 區間加 + 區間和：兩棵 BIT。區間 [l,r] 加 v；前綴和 sum(1..i) = B1(i)*i - B2(i)
static void rbit_add(int64_t *B1, int64_t *B2, uint32_t n, uint32_t l, uint32_t r, int64_t v) {
    bit_add(B1, n, l, v); bit_add(B1, n, r + 1, -v);
    bit_add(B2, n, l, v * (int64_t)(l - 1)); bit_add(B2, n, r + 1, -v * (int64_t)r);
}
static int64_t rbit_prefix(const int64_t *B1, const int64_t *B2, uint32_t i) { return bit_sum(B1, i) * (int64_t)i - bit_sum(B2, i); }
// 逆序數對 O(n log n)：值先壓縮到 1..n，由右往左，答案 += bit_sum(v-1)，再 bit_add(v,1)
```

### 6.4 非遞迴線段樹

```c
// 線段樹（非遞迴、由下往上）：單點設值 + 區間查詢，任何可結合運算（min/max/sum/gcd）都行
// 陣列 2n 個。查詢 [l, r) 左閉右開
#define SEGN 65536
static int32_t sg[2 * SEGN];
static uint32_t sgn;
#define SEG_OP(a, b) ((a) < (b) ? (a) : (b))     // 換成 + 就是區間和，max 就是區間最大
#define SEG_ID INT32_MAX                         // OP 的單位元（min → 最大值，sum → 0）
static void seg_build(const int32_t *a, uint32_t n) {
    sgn = n;
    for (uint32_t i = 0; i < n; i++) sg[n + i] = a[i];
    for (uint32_t i = n - 1; i > 0; i--) sg[i] = SEG_OP(sg[2 * i], sg[2 * i + 1]);
}
static void seg_set(uint32_t i, int32_t v) {
    for (sg[i += sgn] = v; i > 1; i >>= 1) sg[i >> 1] = SEG_OP(sg[i], sg[i ^ 1]);
}
static int32_t seg_query(uint32_t l, uint32_t r) {
    int32_t res = SEG_ID;
    for (l += sgn, r += sgn; l < r; l >>= 1, r >>= 1) {
        if (l & 1) { res = SEG_OP(res, sg[l]); l++; }
        if (r & 1) { --r; res = SEG_OP(res, sg[r]); }
    }
    return res;
}
```

### 6.5 Sparse table

```c
// Sparse table：陣列不再改變時的區間最小值，建表 O(n log n)、查詢 O(1)
// RAM = 4 * n * LOG bytes（n=16384, LOG=15 → 960 KB 太大！n=4096 → 208 KB）。會改值就用線段樹
#define STN 4096
#define STLOG 13
static int32_t st[STLOG][STN];
static void sparse_build(const int32_t *a, uint32_t n) {
    memcpy(st[0], a, n * sizeof *a);
    for (uint32_t j = 1; (1u << j) <= n; j++)
        for (uint32_t i = 0; i + (1u << j) <= n; i++) {
            int32_t x = st[j - 1][i], y = st[j - 1][i + (1u << (j - 1))];
            st[j][i] = x < y ? x : y;
        }
}
static int32_t sparse_min(uint32_t l, uint32_t r) {           // [l, r] 含端點
    uint32_t j = 31 - (uint32_t)__builtin_clz(r - l + 1);
    int32_t x = st[j][l], y = st[j][r + 1 - (1u << j)];
    return x < y ? x : y;
}
```

### 6.6 單調佇列 / 單調堆疊

```c
// 滑動視窗最大值：單調遞減雙端佇列，O(n) 取代 O(nk)。out[i] = max(a[i..i+k-1])
static void window_max(const int32_t *a, uint32_t n, uint32_t k, int32_t *out, uint32_t *dqbuf) {
    uint32_t h = 0, t = 0;                         // dqbuf 存索引，大小 n
    for (uint32_t i = 0; i < n; i++) {
        while (t > h && a[dqbuf[t - 1]] <= a[i]) t--;   // 後面比較大，前面的永遠不會是答案
        dqbuf[t++] = i;
        if (dqbuf[h] + k <= i) h++;                     // 掉出視窗
        if (i + 1 >= k) out[i + 1 - k] = a[dqbuf[h]];
    }
}
// 下一個更大元素：單調堆疊 O(n)。nxt[i] = 右邊第一個 > a[i] 的索引，沒有就是 n
// 「每根柱子能延伸多遠」「直方圖最大矩形」「股票跨度」都是這招
static void next_greater(const int32_t *a, uint32_t n, uint32_t *nxt, uint32_t *stkbuf) {
    uint32_t sp = 0;
    for (uint32_t i = 0; i < n; i++) {
        while (sp && a[stkbuf[sp - 1]] < a[i]) nxt[stkbuf[--sp]] = i;
        stkbuf[sp++] = i;
    }
    while (sp) nxt[stkbuf[--sp]] = n;
}
```

### 6.7 雙指標、Kadane

```c
// 雙指標：非負陣列中「和 <= S 的最長連續子陣列」，O(n) 取代 O(n^2)
static uint32_t longest_sum_le(const uint32_t *a, uint32_t n, uint64_t S) {
    uint64_t sum = 0;
    uint32_t best = 0, l = 0;
    for (uint32_t r = 0; r < n; r++) {
        sum += a[r];
        while (sum > S) sum -= a[l++];
        if (r + 1 - l > best) best = r + 1 - l;
    }
    return best;
}
// 已排序陣列中，和為 target 的數對個數（兩端往中間夾），O(n)
static uint64_t count_pairs_sum(const int32_t *a, uint32_t n, int64_t target) {
    uint64_t cnt = 0;
    uint32_t i = 0, j = n ? n - 1 : 0;
    while (n && i < j) {
        int64_t s = (int64_t)a[i] + a[j];
        if (s < target) i++;
        else if (s > target) j--;
        else if (a[i] == a[j]) { uint64_t k = j - i + 1; cnt += k * (k - 1) / 2; break; }
        else {
            uint32_t ci = 1, cj = 1;
            while (i + ci < j && a[i + ci] == a[i]) ci++;
            while (j - cj > i + ci - 1 && a[j - cj] == a[j]) cj++;
            cnt += (uint64_t)ci * cj; i += ci; j -= cj;
        }
    }
    return cnt;
}
// Kadane：最大連續子陣列和 O(n)
static int64_t max_subarray(const int32_t *a, uint32_t n) {
    int64_t best = a[0], cur = 0;
    for (uint32_t i = 0; i < n; i++) {
        cur = (cur > 0 ? cur : 0) + a[i];
        if (cur > best) best = cur;
    }
    return best;
}
```

### 6.8 二分搜尋、對答案二分

```c
// lower_bound：第一個 >= x 的位置（沒有就回傳 n）。無分支寫法，迴圈次數固定 log n
static uint32_t lower_bound_u32(const uint32_t *a, uint32_t n, uint32_t x) {
    const uint32_t *base = a;
    while (n > 1) {
        uint32_t half = n / 2;
        base = (base[half - 1] < x) ? base + half : base;
        n -= half;
    }
    return (uint32_t)(base - a) + (n == 1 && *base < x);
}
// 對答案二分：ok(x) 對 x 單調（false…false true…true），找最小的 true。O(log(hi-lo) × 檢查成本)
// 例：「最小的最大值」「最少天數」「能切成 k 段的最大長度」
static uint32_t bs_k; static const uint32_t *bs_a; static uint32_t bs_n;
static int ok_split(uint64_t cap) {               // 範例：陣列能不能切成 <= bs_k 段且每段和 <= cap
    uint64_t run = 0; uint32_t parts = 1;
    for (uint32_t i = 0; i < bs_n; i++) {
        if (bs_a[i] > cap) return 0;
        if (run + bs_a[i] > cap) { parts++; run = 0; }
        run += bs_a[i];
    }
    return parts <= bs_k;
}
static uint64_t bs_answer(uint64_t lo, uint64_t hi) {   // 保證 ok(hi) 為真
    while (lo < hi) {
        uint64_t mid = lo + (hi - lo) / 2;
        if (ok_split(mid)) hi = mid; else lo = mid + 1;
    }
    return lo;
}
```

---

## 7. DP 與搜尋技巧

### 7.1 LIS（需要 6.8 的 `lower_bound_u32`）

```c
// 最長嚴格遞增子序列 O(n log n)：tail[k] = 長度 k+1 的遞增子序列最小結尾
// 「不嚴格遞增」把 lower_bound 改成找第一個 > x（upper_bound）
static uint32_t lis_len(const uint32_t *a, uint32_t n, uint32_t *tail) {
    uint32_t len = 0;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t p = lower_bound_u32(tail, len, a[i]);
        tail[p] = a[i];
        if (p == len) len++;
    }
    return len;
}
```

### 7.2 背包、位元集合子集和

```c
// 0/1 背包：一維陣列、容量「由大往小」掃，O(nC) 時間、O(C) 記憶體
static uint32_t knap01(const uint32_t *w, const uint32_t *v, uint32_t n, uint32_t C, uint32_t *dp) {
    memset(dp, 0, (C + 1) * sizeof *dp);
    for (uint32_t i = 0; i < n; i++)
        for (uint32_t c = C; c >= w[i] && c != UINT32_MAX; c--)
            if (dp[c - w[i]] + v[i] > dp[c]) dp[c] = dp[c - w[i]] + v[i];
    return dp[C];
}
// 無限背包（每樣可拿無限次）：同樣一維，但容量「由小往大」掃
static uint32_t knap_unbounded(const uint32_t *w, const uint32_t *v, uint32_t n, uint32_t C, uint32_t *dp) {
    memset(dp, 0, (C + 1) * sizeof *dp);
    for (uint32_t i = 0; i < n; i++)
        for (uint32_t c = w[i]; c <= C; c++)
            if (dp[c - w[i]] + v[i] > dp[c]) dp[c] = dp[c - w[i]] + v[i];
    return dp[C];
}
// 子集和可行性（只問「湊不湊得到」）：位元集合 bits |= bits << w，一次 32 個容量，O(nC/32)
static void subset_sum_bits(const uint32_t *w, uint32_t n, uint32_t C, uint32_t *bits) {
    uint32_t words = C / 32 + 1;
    memset(bits, 0, words * sizeof *bits);
    bits[0] = 1;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t ws = w[i] >> 5, bs = w[i] & 31;
        for (uint32_t j = words; j-- > ws;) {        // 由高往低，像 0/1 背包
            uint32_t lo = bits[j - ws] << bs;
            if (bs && j - ws > 0) lo |= bits[j - ws - 1] >> (32 - bs);
            bits[j] |= lo;
        }
    }
    if ((C + 1) & 31) bits[words - 1] &= (1u << ((C + 1) & 31)) - 1;   // 清掉超過 C 的位元
}
```

### 7.3 位元 DP：TSP

```c
// 位元 DP：旅行推銷員（TSP）O(2^n n^2)，取代 O(n!)。n=12 → 2^12*12*4 = 196 KB；n=16 要 4 MB 放不下
#define TSPN 12
static uint32_t tsp_dp[1u << TSPN][TSPN];
static uint32_t tsp(uint32_t n, uint32_t d[TSPN][TSPN]) {         // 從 0 出發、走完回到 0 的最短
    uint32_t full = (1u << n) - 1, best = 0xFFFFFFFFu;
    for (uint32_t m = 0; m <= full; m++) for (uint32_t i = 0; i < n; i++) tsp_dp[m][i] = 0xFFFFFFFFu;
    tsp_dp[1][0] = 0;
    for (uint32_t m = 1; m <= full; m += 2)                       // 只看含 0 的集合
        for (uint32_t i = 0; i < n; i++) {
            uint32_t cur = tsp_dp[m][i];
            if (cur == 0xFFFFFFFFu) continue;
            for (uint32_t rest = full & ~m; rest; rest &= rest - 1) {
                uint32_t j = (uint32_t)__builtin_ctz(rest), nm = m | (1u << j);
                if (cur + d[i][j] < tsp_dp[nm][j]) tsp_dp[nm][j] = cur + d[i][j];
            }
        }
    for (uint32_t i = 0; i < n; i++)
        if (tsp_dp[full][i] != 0xFFFFFFFFu && tsp_dp[full][i] + d[i][0] < best) best = tsp_dp[full][i] + d[i][0];
    return n == 1 ? 0 : best;
}
// SOS DP（子集和）：F[m] = Σ_{s ⊆ m} f[s]，O(n 2^n) 取代 O(3^n)。原地做
static void sos(uint32_t *f, uint32_t n) {
    for (uint32_t b = 0; b < n; b++)
        for (uint32_t m = 0; m < (1u << n); m++)
            if (m >> b & 1) f[m] += f[m ^ (1u << b)];
}
```

### 7.4 SOS DP

（在 7.3 的程式碼最後的 `sos()`。）

### 7.5 折半搜尋

```c
// 折半搜尋：n <= 40 的子集和計數（和 <= T 的子集數），O(2^(n/2)) 取代 O(2^n)
// 每半邊「合併式」產生已排序子集和（不用排序）。RAM = 3 × 4 × 2^(n/2) bytes：n=30 → 384 KB
#define MH 15
static uint32_t ma[1u << MH], mb[1u << MH], mtmp[1u << MH];
static uint32_t gen_sorted_sums(const uint32_t *w, uint32_t k, uint32_t *out) {
    uint32_t len = 1;
    out[0] = 0;
    for (uint32_t i = 0; i < k; i++) {                       // 合併 out 與 out + w[i]
        uint32_t a = 0, b = 0, t = 0;
        while (a < len && b < len)
            mtmp[t++] = (out[a] <= out[b] + w[i]) ? out[a++] : out[b++] + w[i];
        while (a < len) mtmp[t++] = out[a++];
        while (b < len) mtmp[t++] = out[b++] + w[i];
        len = t;
        memcpy(out, mtmp, len * sizeof *out);
    }
    return len;
}
static uint64_t mitm_count_le(const uint32_t *w, uint32_t n, uint32_t T) {   // 子集和要 < 2^32
    uint32_t h = n / 2, la = gen_sorted_sums(w, h, ma), lb = gen_sorted_sums(w + h, n - h, mb);
    uint64_t cnt = 0;
    uint32_t j = lb;
    for (uint32_t i = 0; i < la; i++) {                       // a 由小到大，b 的指標由大往小
        while (j > 0 && (uint64_t)ma[i] + mb[j - 1] > T) j--;
        cnt += j;
    }
    return cnt;
}
```

### 7.6 記憶化 vs 表格化

- 遞迴＋記憶化寫起來快，但 MCU 堆疊只有幾 KB，深度一大就當機；**一律改成表格化（由小到大填表）**。
- 表格化之後才看得出「只用到前一列」→ 把二維表壓成一維（背包就是這樣）。
- 狀態轉移只依賴固定幾個前項且 n 很大 → 改成矩陣快速冪（3.14）。

### 7.7 座標壓縮（需要 4.1、6.8）

```c
// 座標壓縮：把大範圍的值換成 0..k-1（排序 + 去重 + 二分）。之後就能開陣列、BIT
static uint32_t compress(uint32_t *vals, uint32_t n, uint32_t *tmp) {     // vals 會變成去重後的排序值
    radix_sort_u32(vals, tmp, n);
    uint32_t k = 0;
    for (uint32_t i = 0; i < n; i++) if (!k || vals[i] != vals[k - 1]) vals[k++] = vals[i];
    return k;                                                 // 值 x 的編號 = lower_bound_u32(vals, k, x)
}
```

---

## 8. 字串與卷積

### 8.1 KMP

```c
// KMP：在文字 t 中找樣式 p 的所有出現位置，O(n+m) 取代 O(nm)。pi 大小 m
// pi[i] = p[0..i] 最長「既是前綴也是後綴」的長度（真前綴）
static void prefix_function(const char *p, uint32_t m, uint32_t *pi) {
    pi[0] = 0;
    for (uint32_t i = 1, k = 0; i < m; i++) {
        while (k && p[i] != p[k]) k = pi[k - 1];
        if (p[i] == p[k]) k++;
        pi[i] = k;
    }
}
static uint32_t kmp_count(const char *t, uint32_t n, const char *p, uint32_t m, const uint32_t *pi, uint32_t *pos) {
    uint32_t cnt = 0;
    for (uint32_t i = 0, k = 0; i < n; i++) {
        while (k && t[i] != p[k]) k = pi[k - 1];
        if (t[i] == p[k]) k++;
        if (k == m) { if (pos) pos[cnt] = i + 1 - m; cnt++; k = pi[k - 1]; }
    }
    return cnt;
}
// 最小週期：字串 s 由長度 L = m - pi[m-1] 的片段重複而成 ⇔ m % L == 0
```

### 8.2 Z 函數

```c
// Z 函數：z[i] = s 與 s[i..] 的最長共同前綴，O(n)。找樣式：對 p + '#' + t 算 z，z == |p| 的位置就是出現
static void z_function(const char *s, uint32_t n, uint32_t *z) {
    uint32_t l = 0, r = 0;
    if (!n) return;
    z[0] = n;
    for (uint32_t i = 1; i < n; i++) {
        uint32_t k = 0;
        if (i < r) { k = r - i; if (z[i - l] < k) k = z[i - l]; }
        while (i + k < n && s[k] == s[i + k]) k++;
        z[i] = k;
        if (i + k > r) { l = i; r = i + k; }
    }
}
```

### 8.3 滾動雜湊（無除法）

```c
// 滾動雜湊：子字串比較 O(1)。模 2^31-1（梅森質數）用位移取代 %，沒有任何除法
// 兩組不同底數 = 雙雜湊，碰撞機率約 n^2 / 2^62。RAM：每組 H、P 各 4(n+1) bytes
#define MERS 0x7FFFFFFFu
static inline uint32_t mers_mod(uint64_t x) {            // x < 2^62
    x = (x & MERS) + (x >> 31);
    x = (x & MERS) + (x >> 31);
    return (uint32_t)(x >= MERS ? x - MERS : x);
}
typedef struct { uint32_t *H, *P; } rhash_t;              // H[i] = s[0..i-1] 的雜湊，P[i] = base^i
static void rhash_build(rhash_t *h, const char *s, uint32_t n, uint32_t base) {
    h->H[0] = 0; h->P[0] = 1;
    for (uint32_t i = 0; i < n; i++) {
        h->H[i + 1] = mers_mod((uint64_t)h->H[i] * base + (uint8_t)s[i] + 1);
        h->P[i + 1] = mers_mod((uint64_t)h->P[i] * base);
    }
}
static inline uint32_t rhash_get(const rhash_t *h, uint32_t l, uint32_t len) {   // s[l .. l+len-1]
    uint32_t a = h->H[l + len], b = mers_mod((uint64_t)h->H[l] * h->P[len]);
    return a >= b ? a - b : a + MERS - b;
}
// 用法：rhash_build(&h1, s, n, 131); rhash_build(&h2, s, n, 1000003);
// 兩個子字串相等 ⇔ 兩組雜湊都相等。最長重複子字串 = 對長度二分 + 雜湊（排序後找相同）
```

### 8.4 NTT 卷積（需要 3.2、3.9）

```c
// NTT 卷積（多項式乘法、大數乘法、「所有 a_i + b_j 的出現次數」）：O(n log n) 取代 O(n^2)
// 模 998244353 = 119·2^23+1，原根 3。用前面的 Montgomery 乘法，迴圈內沒有除法
// 係數結果要 < 998244353（計數題通常成立）。RAM = 8 × NTTN bytes（2^15 → 256 KB）
#define NTT_MOD 998244353u
#define NTTN (1u << 15)
static uint32_t na_[NTTN], nb_[NTTN];
static void ntt(uint32_t *a, uint32_t n, int invert, const mont_t *M) {   // a 為 Montgomery 形式
    for (uint32_t i = 1, j = 0; i < n; i++) {                              // 位元反轉排列
        uint32_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) { uint32_t t = a[i]; a[i] = a[j]; a[j] = t; }
    }
    for (uint32_t len = 2; len <= n; len <<= 1) {
        uint32_t w = pow_mod(3, (NTT_MOD - 1) / len, NTT_MOD);
        if (invert) w = pow_mod(w, NTT_MOD - 2, NTT_MOD);
        uint32_t wm = to_mont(M, w), half = len >> 1;
        for (uint32_t i = 0; i < n; i += len) {
            uint32_t wc = to_mont(M, 1);
            for (uint32_t j = 0; j < half; j++) {
                uint32_t u = a[i + j], v = mont_mul(M, a[i + j + half], wc);
                a[i + j] = u + v >= NTT_MOD ? u + v - NTT_MOD : u + v;
                a[i + j + half] = u >= v ? u - v : u + NTT_MOD - v;
                wc = mont_mul(M, wc, wm);
            }
        }
    }
    if (invert) {
        uint32_t ninv = to_mont(M, pow_mod(n, NTT_MOD - 2, NTT_MOD));
        for (uint32_t i = 0; i < n; i++) a[i] = mont_mul(M, a[i], ninv);
    }
}
// out[k] = Σ a[i] b[k-i]，長度 la+lb-1，必須 <= NTTN
static uint32_t convolve(const uint32_t *a, uint32_t la, const uint32_t *b, uint32_t lb, uint32_t *out) {
    mont_t M = mont_init(NTT_MOD);
    uint32_t need = la + lb - 1, n = 1;
    while (n < need) n <<= 1;
    memset(na_, 0, n * sizeof na_[0]); memset(nb_, 0, n * sizeof nb_[0]);
    for (uint32_t i = 0; i < la; i++) na_[i] = to_mont(&M, a[i]);
    for (uint32_t i = 0; i < lb; i++) nb_[i] = to_mont(&M, b[i]);
    ntt(na_, n, 0, &M); ntt(nb_, n, 0, &M);
    for (uint32_t i = 0; i < n; i++) na_[i] = mont_mul(&M, na_[i], nb_[i]);
    ntt(na_, n, 1, &M);
    for (uint32_t i = 0; i < need; i++) out[i] = from_mont(&M, na_[i]);
    return need;
}
```

---

## 9. 找規律：數學題的硬解方法

很多「看起來要模擬很久」的題目其實有公式或週期。小模型通常看不出來，用這個流程自己找：

1. **先寫最笨的暴力**，印出 n = 0..20 的答案。
2. **看差分**：相鄰相減。差分是常數 → 一次式；二次差分常數 → 二次式（3 個點就能解出係數）。差分是原數列的倍數 → 指數。
3. **比對第 3.19 節的數列**：1,2,5,14,42 是卡特蘭；1,1,2,3,5,8 是 Fibonacci；1,2,6,24 是階乘；2 的冪減一、三角數 n(n+1)/2 也很常見。
4. **看是否週期性**：印出 f(n) mod m，用循環偵測（3.15）找週期，答案就是 f(n mod 週期)。
5. **看是否可寫成遞迴**：f(n) 只依賴前 K 項 → 矩陣快速冪（3.14），n 再大都是 O(K³ log n)。
6. **看是否與因數有關**：答案只在 n 的因數處改變、或跟 ⌊n/i⌋ 有關 → 整除分塊（3.16），O(√n)。
7. **看是否是計數**：「有幾種方法」先試組合公式（星與槓、格點路徑），再試 DP。
8. 猜到公式後，**拿暴力版對 n = 0..1000 全部比對**，一致才換掉。

圖論的「看出來」訊號：
- 「互相依賴」「先做 A 才能做 B」→ 拓樸排序；有環就無解。
- 「最少幾步」「最少換幾次」→ BFS（每步代價相同）；代價不同 → Dijkstra；代價只有 0/1 → 0-1 BFS。
- 「分組」「是否相連」「合併」→ 並查集。
- 「兩兩配對」「分配工作」「最多能滿足幾個」→ 二分匹配／最大流。
- 「所有點都要經過一次」且 n ≤ 12 → 位元 DP；n 大就幾乎一定不是要你求最佳解。
- 方格題就是圖：每格是節點、上下左右是邊。

---

## 10. 給小模型的 prompt

小模型（約 8k context）一次只能看一小段。用法：先用 10.1 分類，再用 10.2 套範本，最後自己對拍。每次只貼「一個」範本，不要整份貼。

### 10.1 分類 prompt（先跑這個，還不要寫程式）

```text
你是演算法顧問。先不要寫程式。讀下面的題目，從「分類表」挑出最適合的 1～2 招。

【分類表】格式：訊號 → 招式 → 複雜度
A1 迴圈內一直 x*y%p（同一個 p） → Montgomery 乘法 → 去掉 64 位元除法
A2 迴圈內除以同一個執行期常數 → fastdiv/fastmod → 除法變乘法
A3 判斷一個大數是否質數 → Miller-Rabin → O(log n)
A4 1..N 的質數/很多次質數查詢 → 篩法（N>1e7 用分段篩） → O(N log log N)
A5 每個數的 phi/mu/最小質因數 → 線性篩 → O(N)
A6 a^e mod p、超大指數 → 快速冪；指數先 mod (p-1) → O(log e)
A7 很多次 C(n,r) mod p → 階乘表+反元素 → O(1)/次
A8 遞迴式第 n 項、n 很大 → 矩陣快速冪 → O(K^3 log n)
A9 sum floor(n/i) 或與因數有關的總和 → 整除分塊 → O(sqrt n)
A10 重複套用同一個函式非常多次 → 循環偵測 → O(週期)
A11 多個「除以 m 餘 a」條件 → 中國剩餘定理
A12 能被幾個數之一整除的個數 → 排容原理 → O(2^k)
B1 最少步數、每步代價相同、方格迷宮 → BFS → O(N+M)
B2 非負權最短路 → Dijkstra+堆積 → O(M log M)
B3 權重只有 0/1 → 0-1 BFS
B4 有負權 → Bellman-Ford
B5 全點對、N<=200 → Floyd；只問可不可達 → 位元集合閉包 O(N^3/32)
B6 先後順序、依賴、有無環 → 拓樸排序
B7 合併群組、是否相連 → 並查集
B8 最小生成樹 → Kruskal
B9 互相可達的群組/2-SAT → SCC
B10 樹上距離/共同祖先 → LCA
B11 配對、分配、最大流量、最小割 → Dinic
C1 陣列不變、很多次區間和 → 前綴和 O(1)
C2 很多次區間加值、最後才看 → 差分陣列
C3 邊改值邊問區間和 → Fenwick；問 min/max → 線段樹
C4 固定長度視窗最大最小 → 單調佇列 O(n)
C5 右邊第一個更大 → 單調堆疊 O(n)
C6 連續子陣列且元素非負 → 雙指標 O(n)
C7 要排序整數 → 基數排序 O(n)；只要第 k 小 → quickselect O(n)
C8 答案有單調性（越大越容易成立） → 對答案二分
D1 最長遞增子序列 → O(n log n)
D2 背包；只問湊不湊得到 → 位元集合
D3 n<=12 要走過全部/排列 → 位元 DP O(2^n n^2)
D4 n<=40 枚舉子集 → 折半搜尋 O(2^(n/2))
E1 字串找樣式 → KMP O(n+m)
E2 比較很多子字串 → 滾動雜湊 O(1)/次
E3 多項式乘法/所有兩數和的次數 → NTT O(n log n)
F1 以上都不像、只給小 n → 暴力打表找規律

【題目】
{貼題目，或貼原始程式並說「這段太慢」}

【請只回答這四行】
1. 現在的寫法複雜度：O(...)，n 的上限是 ...，大約要算 ... 次
2. 選的招式代號：...（理由一句話：題目哪個字句符合訊號）
3. 換招後複雜度：O(...)，大約 ... 次
4. 需要注意：（溢位？n=0 或 1？取模的負數？RAM 是否超過 400 KB？）
不確定就寫「不確定」，不要猜。
```

### 10.2 套範本 prompt（每次一個範本）

```text
你是 C 工程師，目標平台是 RP2350（RISC-V，沒有 FPU、沒有 64 位元除法，RAM 520 KB，堆疊只有幾 KB）。

【任務】把下面的「範本」改成能解「題目」的函式。原則：範本的演算法部分一行都不要改，只改：
- 輸入怎麼讀進範本的陣列
- #define 的陣列大小（依題目上限）
- 輸出
如果範本需要改演算法本身，先停下來說明為什麼，不要直接改。

【規則】
- 只用 C99/C11、<stdint.h>、<string.h>。不要 malloc、不要遞迴、不要 float/double。
- 乘法結果可能超過 32 位元時一律先轉 (uint64_t)。
- 取模後的負數：寫成 ((x % m) + m) % m。
- 迴圈內不要出現 / 或 %，除非除數是編譯期常數。
- 陣列總大小算給我看（bytes），超過 400 KB 就改小或換做法。

【範本】
{從 algorithms.md 複製一段，含註解}

【題目 / 原始程式】
{貼上}

【輸出】
1. 改好的完整函式
2. 一個暴力版函式 xxx_naive（最直接的寫法）
3. 一段比對程式：隨機產生 1000 組小輸入，兩者結果不同就印出輸入並停止
4. 自我檢查清單逐項回答：
   - n=0、n=1、全部相同、已排序、反向排序 是否正確？
   - 最大輸入時哪個變數最大？會不會溢位 uint32？
   - 有沒有除以 0 的可能？
   - 陣列索引最大是多少？有沒有超出 #define？
```

### 10.3 各主題的補充句（接在 10.2 後面）

| 主題 | 加這句 |
|---|---|
| 取模運算 | 「模數是否為質數？不是質數就不能用費馬求反元素，改用 inv_exgcd；乘之前兩個數都要先 < m。」 |
| Montgomery | 「進迴圈前所有數用 to_mont 轉換，迴圈內只用 mont_mul，最後才 from_mont。模數必須是奇數且 < 2^31。」 |
| 篩法 | 「SIEVE_N 設成題目上限；若 N/16 bytes 超過 300 KB 就改用 count_primes_seg。」 |
| 矩陣快速冪 | 「先寫出轉移矩陣並用 n=0..10 和暴力遞迴比對，再做快速冪。注意次方是 n-K+1。」 |
| 圖論 | 「節點編號是否從 1 開始？要換成 0 開始。無向圖每條邊要加兩次，MAXM 要算兩倍。」 |
| Dijkstra | 「權重有沒有負的？有就不能用。距離總和會不會超過 2^32？會就把打包改成 dist 用 uint64 另存。」 |
| DP | 「先寫出狀態定義與轉移式（一行數學），再寫程式。只依賴前一列就壓成一維。」 |
| 二分答案 | 「先證明 ok(x) 對 x 單調；lo、hi 的初值要保證 ok(hi) 一定成立。」 |
| 字串 | 「字串長度用變數傳，不要用 strlen（題目可能含 0）。」 |

### 10.4 人工檢查（每次都做）

1. 編譯加 `-Wall -Wextra`，警告全部看過。
2. 對拍：隨機小輸入 1000 組，暴力版與新版完全相同。
3. 邊界：n=0、n=1、最大 n、全部相同、有重複值。
4. 拿最大輸入計時，和原版比；沒變快就退回。
5. 模型給的函式名，用 Ctrl+F 在本檔搜尋；搜不到就是它編的。

---

## 11. RAM 預算與未收錄清單

### 11.1 RAM 速查（RP2350：520 KB SRAM，留 50 KB 以上給 SDK 與堆疊）

| 結構 | 每單位 | 例 |
|---|---|---|
| `uint32_t a[N]` | 4N | N=10⁵ → 400 KB（已經很緊） |
| 位元陣列 | N/8 | N=10⁶ → 125 KB |
| 奇數位元篩 | N/16 | N=4×10⁶ → 250 KB |
| CSR 圖（含權重） | 4(N + 2M) | N=2萬、M=5萬 → 480 KB |
| 鄰接矩陣 uint32 | 4N² | N=300 → 360 KB；只存可達性用位元：N=1024 → 128 KB |
| 位元 DP `dp[2^n][n]` uint32 | 4n·2ⁿ | n=12 → 196 KB；n=13 → 425 KB；n=16 → 4 MB（放不下） |
| Sparse table | 4N log N | N=4096 → 208 KB |
| 線段樹 | 8N（int32） | N=65536 → 512 KB（太大，改 N=32768） |
| NTT | 8 × 長度 | 2¹⁵ → 256 KB |
| 折半搜尋 | 12 × 2^(n/2) | n=30 → 384 KB |

省 RAM 的招：索引改 `uint16_t`（< 65536 個時）；距離陣列改成「一層一層 BFS 數層數」（5.19）；只存需要的那一列 DP；用位元取代 `uint8_t` 布林陣列。

### 11.2 未收錄（時間不夠測，或考試機率低）

- 帶懶標記的線段樹（區間加 + 區間 min）：用 6.3 的雙 BIT 只能處理區間加 + 區間和。
- 凸包、凸包優化（Convex Hull Trick）、李超線段樹、Knuth / 分治優化 DP。
- Mo 演算法、重輕鏈剖分、樹上差分、歐拉序。
- 後綴陣列、Aho–Corasick、Manacher。
- Hungarian（帶權指派）、最小費用流、Edmonds 一般圖匹配。
- Meissel–Lehmer（π(N) 到 10¹¹ 以上）、杜教篩、Min_25 篩。
- FFT（浮點，RISC-V 沒有 FPU，用 8.4 的 NTT）。

### 11.3 未驗證

- RP2350 實機週期：本檔 RV32 數字是 qemu 指令計數 × 假設權重（除法 18、其餘 1），沒有算管線停頓、分支、記憶體等待；Hazard3 的 `mulh/mulhu` 是否 1 週期 未驗證。
- Arm Cortex-M33 上的倍數：沒有量（`udiv` 較快，除法相關的倍數會比 RV32 小）。
- 所有範本沒有在 Pico 2 實機上跑過。
