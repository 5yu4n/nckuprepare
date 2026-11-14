# 建置、燒錄、計時、除錯（RP2350 + pico-sdk）

> **查證方式**：CMake 範本用 pico-sdk 2.3.1 實際跑過 `cmake` 設定（`PICO_PLATFORM=rp2350-riscv`、`PICO_BOARD=pico2`），PIO 標頭也實際產生出來；確認產生的編譯參數是 `-O3`、`-march=rv32ima_…_zbkb_zca_zcb_zcmp`、`-mstrict-align`。完整連結需要 RISC-V GCC 與 newlib，這台機器沒有，所以**沒有產出 .uf2**。`bench.h`、`fault_report.c` 用 SDK 標頭對 RISC-V 與 Arm 編譯檢查，`bench.h` 也在 Linux 上實際跑過。`picotool` 參數對照它的 README；序列埠工具是一般 Linux 用法，**未在考場環境確認**。
> 相關檔案：[00-START.md](00-START.md)、[cheatsheet.md](cheatsheet.md)、[rp2350-riscv.md](rp2350-riscv.md)、[prompts.md](prompts.md)（H1 修正迴圈）。

## 1. CMakeLists.txt 範本

**考場若已經給了 CMakeLists.txt，照它的，只加你需要的 `target_link_libraries`。** 下面是從零開始用的。

```cmake
cmake_minimum_required(VERSION 3.13)

# 1. 板子與架構：要在 include(pico_sdk_import.cmake) 之前設定
#    考場板子是 RP2350B：用 templates/board-app/boards/exam_rp2350b.h（PICO_RP2350A=0，48 支 GPIO）
#    不要用 pico2（那是 RP2350A，只有 GPIO 0–29）。見 board.md 第 0 節。
list(APPEND PICO_BOARD_HEADER_DIRS ${CMAKE_CURRENT_LIST_DIR}/boards)   # 放 exam_rp2350b.h 的資料夾
if (NOT PICO_BOARD)
    set(PICO_BOARD exam_rp2350b)
endif()
if (NOT PICO_PLATFORM)
    set(PICO_PLATFORM rp2350-riscv)       # Arm 版：rp2350-arm-s
endif()

include(pico_sdk_import.cmake)            # 從 pico-sdk/external/ 複製過來
project(app C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
pico_sdk_init()

add_executable(app
    main.c
    # asm_kernels.S                       # 有手寫組語才加
)

# 2. PIO：產生 xform.pio.h（main.c 裡 #include "xform.pio.h"）
pico_generate_pio_header(app ${CMAKE_CURRENT_LIST_DIR}/xform.pio)

# 3. 用到哪些硬體就連哪些
target_link_libraries(app
    pico_stdlib          # time_us_64、printf、gpio
    pico_multicore       # multicore_launch_core1、FIFO
    hardware_dma
    hardware_pio
    hardware_sync        # spin lock、__dmb
    hardware_irq
)

# 4. printf 從哪出去：USB 或 UART（兩個都開也可以）
pico_enable_stdio_usb(app 1)
pico_enable_stdio_uart(app 0)

# 5. 選用：整個程式放 RAM（不受 flash / XIP 快取影響），題目允許改 CMake 才用
# pico_set_binary_type(app copy_to_ram)

# 6. 產生 .uf2、.dis（反組譯）、.map 等
pico_add_extra_outputs(app)
```

| 用到 | `#include` | 要連的庫 |
|---|---|---|
| `printf`、`time_us_64`、`sleep_ms`、GPIO | `pico/stdlib.h`、`<stdio.h>` | `pico_stdlib` |
| DMA | `hardware/dma.h` | `hardware_dma` |
| PIO | `hardware/pio.h`、`xxx.pio.h` | `hardware_pio` + `pico_generate_pio_header` |
| 雙核 | `pico/multicore.h` | `pico_multicore` |
| spin lock、`__dmb`、關中斷 | `hardware/sync.h` | `hardware_sync` |
| `critical_section_*` | `pico/critical_section.h` | `pico_sync` |
| 中斷 | `hardware/irq.h` | `hardware_irq` |
| 例外處理（第 6 節） | `hardware/exception.h` | `hardware_exception` |
| 佇列 `queue_*` | `pico/util/queue.h` | `pico_util` |

漏連庫的錯誤長這樣：`fatal error: hardware/dma.h: No such file or directory` 或 `undefined reference to 'dma_channel_configure'`。

`binary type`（第 5 項，選用）：
- 預設：程式在 flash，透過 16 KB XIP 快取執行。
- `copy_to_ram`：開機時整個複製到 SRAM 再跑，最快、最穩，但程式 + 資料要放得進 520 KB。
- `no_flash`：只存在 RAM，斷電就沒了，要用 `picotool load` 直接載入 RAM。

## 2. 建置

```sh
export PICO_SDK_PATH=/path/to/pico-sdk        # 考場若已設好就跳過
mkdir -p build && cd build
cmake -DPICO_PLATFORM=rp2350-riscv ..   # Arm：rp2350-arm-s（板子由 CMakeLists 設成 exam_rp2350b）
make -j4                                       # 或 cmake -G Ninja 之後用 ninja
ls *.uf2 *.dis                                 # app.uf2 燒錄用；app.dis 反組譯
```

- 預設 `CMAKE_BUILD_TYPE` 是 Release，GCC 實際是 `-O3`（不用自己加）。要除錯才用 `-DCMAKE_BUILD_TYPE=Debug`（`-Og`），**量時間前記得換回來**。
- 換 `PICO_PLATFORM` 或 `PICO_BOARD` 要**刪掉 build 資料夾重來**，CMake 會記住舊設定。
- 第一次設定時 SDK 可能要下載或編譯 `picotool`（警告訊息 `No installed picotool ... building from source`）。考場沒網路又沒裝 picotool 時，問監考或看考場說明。
- 看編譯器實際產生什麼：`app.dis`（`pico_add_extra_outputs` 產生），搜尋函式名稱，確認有 `cpop`、`sh2add`，沒有 `call __divsi3`、`call __mulsf3` 這類函式呼叫。

## 3. 燒錄

| 方法 | 步驟 |
|---|---|
| 拖放 | 按住板子的 BOOTSEL 鍵再插 USB → 電腦出現一個 USB 磁碟 → 把 `app.uf2` 複製進去 → 板子自己重開跑程式 |
| picotool（板子在 BOOTSEL 模式） | `picotool load -x app.uf2`（`-x` 載完直接執行） |
| picotool（程式正在跑、有開 USB stdio） | `picotool load -f -x app.uf2`（`-f` 讓板子自己重開進 BOOTSEL，免按鍵） |
| 驗證 | `picotool load -v -x app.uf2`（`-v` 寫完再讀回比對） |
| 看板子上是什麼程式 | `picotool info -a`（顯示程式名、`PICO_BOARD` 等 binary info） |

考場若是 Online Judge 自動燒錄，以上都不用，**只要確認交的是對的檔案（.c 或 .uf2，看題目）**。

## 4. 看輸出

- USB：Linux `screen /dev/ttyACM0 115200`（離開按 Ctrl-A 再按 K）或 `minicom -D /dev/ttyACM0`；Windows 用序列埠工具連 COM 埠。
- UART：Pico 2 預設 TX = GP0、RX = GP1，鮑率 115200（SDK `pico2.h`、`uart.h`）。
- **USB 輸出開頭會掉**：電腦還沒連上前印的東西會不見。開頭加：

```c
stdio_init_all();
while (!stdio_usb_connected()) sleep_ms(10);   // 等電腦打開序列埠（只在 USB stdio 時用）
```

- 量時間的區段裡**不要 printf**，量完再印。
- 板子 `panic` 時會印 `*** PANIC ***`，後面接原因（例如 `dma_claim_unused_channel(true)` 沒有空通道）。

## 5. 計時與對拍：`bench.h`

一個標頭同時給板子和 Linux 用。`BENCH` 跑幾次取最快，印出微秒；RISC-V 上同時印週期數（會先打開預設是停的 `mcycle`）。`bench_same_u32` 比對原版與優化版輸出，印出第一個不同的位置。

```c
// bench.h：計時 + 對拍。板子（pico-sdk）和 Linux 都能用。
#pragma once
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
  #include "pico/stdlib.h"
  static inline uint64_t bench_now_us(void) { return time_us_64(); }
  #if defined(__riscv)
    static inline void     bench_cycles_on(void) { __asm__ volatile("csrci mcountinhibit, 5"); }
    static inline uint32_t bench_cycles(void)    { uint32_t r; __asm__ volatile("csrr %0, mcycle" : "=r"(r)); return r; }
  #else
    static inline void     bench_cycles_on(void) {}
    static inline uint32_t bench_cycles(void)    { return 0; }   // Arm：只看微秒
  #endif
#else
  #include <time.h>
  static inline uint64_t bench_now_us(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (uint64_t)t.tv_sec * 1000000u + (uint64_t)t.tv_nsec / 1000u; }
  static inline void     bench_cycles_on(void) {}
  static inline uint32_t bench_cycles(void)    { return 0; }
#endif

// 跑 reps 次取最快：BENCH("名稱", 5, 程式碼);
#define BENCH(name, reps, code) do { \
    uint64_t best_us_ = UINT64_MAX; uint32_t best_cy_ = UINT32_MAX; \
    bench_cycles_on(); \
    for (int r_ = 0; r_ < (reps); r_++) { \
        uint32_t c0_ = bench_cycles(); uint64_t t0_ = bench_now_us(); \
        code; \
        uint64_t dt_ = bench_now_us() - t0_; uint32_t dc_ = bench_cycles() - c0_; \
        if (dt_ < best_us_) best_us_ = dt_; \
        if (dc_ < best_cy_) best_cy_ = dc_; \
    } \
    printf("%-24s %10llu us %12lu cycles\n", name, (unsigned long long)best_us_, (unsigned long)best_cy_); \
} while (0)

// 比對兩塊記憶體：相同回傳 1；不同印出第一個不同的位置（以 uint32 為單位）
static inline int bench_same_u32(const char *name, const uint32_t *a, const uint32_t *b, size_t n) {
    for (size_t i = 0; i < n; i++)
        if (a[i] != b[i]) {
            printf("%s: MISMATCH at %u: ref=0x%08lx fast=0x%08lx\n", name, (unsigned)i, (unsigned long)a[i], (unsigned long)b[i]);
            return 0;
        }
    printf("%s: ok (%u words)\n", name, (unsigned)n);
    return 1;
}

// 固定種子的亂數（每次跑結果一樣，不要用 rand()）
static uint32_t bench_rng = 2463534242u;
static inline uint32_t bench_rand(void) { bench_rng ^= bench_rng << 13; bench_rng ^= bench_rng >> 17; bench_rng ^= bench_rng << 5; return bench_rng; }
```

用法範例（Linux 實測：`popcnt_ref` 98 µs → `popcnt_fast` 10 µs，結果一致）：

```c
// 範例：原版 vs 優化版，先比對再計時
#include "bench.h"
#define N 4096
static uint32_t in[N], out_ref[N], out_fast[N];

static void popcnt_ref(const uint32_t *a, uint32_t *o, int n) {
    for (int i = 0; i < n; i++) { uint32_t c = 0; for (int b = 0; b < 32; b++) c += (a[i] >> b) & 1; o[i] = c; }
}
static void popcnt_fast(const uint32_t *a, uint32_t *o, int n) {
    for (int i = 0; i < n; i++) o[i] = (uint32_t)__builtin_popcount(a[i]);
}

int main(void) {
#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
    stdio_init_all();
    sleep_ms(2000);                       // USB 序列埠要等電腦連上，不然前面的輸出會掉
#endif
    // 邊界值 + 亂數
    in[0] = 0; in[1] = 1; in[2] = 0xFFFFFFFFu; in[3] = 0x80000000u;
    for (int i = 4; i < N; i++) in[i] = bench_rand();

    popcnt_ref(in, out_ref, N);
    popcnt_fast(in, out_fast, N);
    if (!bench_same_u32("popcnt", out_ref, out_fast, N)) return 1;   // 錯了就不用量

    BENCH("popcnt_ref", 5, popcnt_ref(in, out_ref, N));
    BENCH("popcnt_fast", 5, popcnt_fast(in, out_fast, N));
    return 0;
}
```

**注意**：
- 被量的程式結果要寫到全域陣列或印出來，否則 `-O3` 會把整段刪掉，量到 0。
- `mcycle` 是 32 位元版本，150 MHz 下約 28 秒繞一圈；量長時間看微秒。
- **先比對、再計時**：錯的程式再快也是 0 分。

## 6. 當機與卡住

### 6.1 先裝上「當機回報」

RISC-V 上 SDK 預設的例外處理是**讓核心靜靜睡著**（`crt0_riscv.S` 註解：RISC-V 沒有 double fault lockup，所以直接讓核心無限期睡眠），看起來就是「程式突然沒反應」，沒有任何訊息。把下面的 `fault_report_install()` 放在 `main()` 開頭，當機時會印出原因和出錯的位址（pc），拿 pc 去 `app.dis` 搜尋就知道是哪一行。

```c
// 當機時印出原因（RISC-V）：放在 main() 最前面呼叫 fault_report_install()
#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/exception.h"

#if defined(__riscv)
static void fault_handler(void) {
    uint32_t cause, pc;
    __asm__ volatile("csrr %0, mcause" : "=r"(cause));
    __asm__ volatile("csrr %0, mepc"   : "=r"(pc));
    // 4/6 = 讀/寫沒對齊，5/7 = 讀/寫到不存在的位址，2 = 非法指令
    // Hazard3 的 mtval 固定是 0，拿不到出錯的位址；用 pc 對 .dis 檔找是哪一行
    printf("\nFAULT mcause=%lu pc=0x%08lx\n", (unsigned long)cause, (unsigned long)pc);
    while (1) tight_loop_contents();
}
static void fault_report_install(void) {
    exception_set_exclusive_handler(LOAD_ALIGN_EXCEPTION,  fault_handler);
    exception_set_exclusive_handler(STORE_ALIGN_EXCEPTION, fault_handler);
    exception_set_exclusive_handler(LOAD_FAULT_EXCEPTION,  fault_handler);
    exception_set_exclusive_handler(STORE_FAULT_EXCEPTION, fault_handler);
    exception_set_exclusive_handler(INSTR_ILLEGAL_EXCEPTION, fault_handler);
}
#else
static void fault_handler(void) {
    printf("\nHARDFAULT\n");
    while (1) tight_loop_contents();
}
static void fault_report_install(void) { exception_set_exclusive_handler(HARDFAULT_EXCEPTION, fault_handler); }
#endif

int main(void) {
    stdio_init_all();
    fault_report_install();
    static uint8_t buf[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    volatile uint32_t *p = (volatile uint32_t *)(buf + 1);   // 故意沒對齊：RISC-V 會進 fault_handler
    printf("%lu\n", (unsigned long)*p);
}
```

（`exception_set_exclusive_handler` 與例外編號對照 SDK `hardware/exception.h`；Hazard3 的 `mtval` 固定為 0，所以拿不到出錯的資料位址。USB stdio 在例外裡可能送不出去，有 UART 就用 UART。）

| `mcause` | 意思 | 最常見的原因 |
|---|---|---|
| 2 | 非法指令 | 堆疊被寫壞後 `ret` 跳到亂七八糟的地方；行內組語用了 SDK `-march` 沒開的指令（例如 `clmul`） |
| 4 / 6 | 讀 / 寫沒對齊 | `*(uint32_t *)(buf + 1)`；`packed` 結構；DMA 緩衝區沒對齊 → 改 `memcpy` 或 `__attribute__((aligned(4)))` |
| 5 / 7 | 讀 / 寫到不存在的位址 | 空指標、陣列越界、函式指標沒設定 |

### 6.2 症狀 → 原因 → 修法

| 症狀 | 先查 | 修法 |
|---|---|---|
| 完全沒有輸出 | USB 還沒連上就印完了；或開頭就當機 | 加 `stdio_usb_connected()` 等待；裝 6.1 |
| 跑一陣子後亂掉、變數莫名改變 | **堆疊溢位**：預設每核堆疊 2 KB（`PICO_STACK_SIZE` / `PICO_CORE1_STACK_SIZE` = 0x800） | 大陣列不要放在函式裡，改 `static` 或全域 |
| 陣列後面的變數被改掉 | **DMA 寫超過**：`count` 給了位元組數；`DMA_SIZE` 和型別不合 | `count = bytes >> size`；`uint32_t` 配 `DMA_SIZE_32` |
| DMA 結果是舊資料或一半 | 沒等完成就讀；等的是 TX 不是 RX | `dma_channel_wait_for_finish_blocking(rx)` |
| PIO 管線卡住 | RX DMA 沒啟動或 `count` 比 TX 少 → RX FIFO 滿了 PIO 停住；SM 沒 enable | 先啟動 RX 再啟動 TX，兩邊 `count` 一致；檢查 `pio_sm_set_enabled(pio, sm, true)` |
| `*** PANIC ***` | `dma_claim_unused_channel(true)`、`pio_claim_unused_sm(pio, true)` 沒有資源了；重複 claim | 通道只 claim 一次重複用；結束時 `dma_channel_unclaim` |
| 卡在 spin lock | `spin_lock_blocking` 會關中斷：鎖裡面呼叫 `printf`（USB）、`sleep_ms` 會卡死；同一把鎖重複拿 | 鎖裡只放幾行運算；不要巢狀 |
| 卡在 `multicore_fifo_pop_blocking` | **core1 沒啟動**（忘了 `multicore_launch_core1`）；core1 已經結束或當機；push/pop 次數對不上 | 確認有 launch、只 launch 一次；兩邊次數一致 |
| 兩核結果偶爾不對 | 共用變數沒保護；讀旗標後沒 `__dmb()` | 本地累加後合併；旗標用 `volatile` + `__dmb()` |
| 中斷處理函式讓系統變慢或卡住 | 處理函式在 flash、裡面 `printf`、沒清中斷旗標 | `__not_in_flash_func`；只設旗標；記得 `dma_channel_acknowledge_irq0` |
| 速度忽快忽慢 | 程式在 flash，被 XIP 快取影響；USB 中斷打斷 | 熱函式 `__not_in_flash_func` 或 `copy_to_ram`；量多次取最快 |
| Debug 版正常、Release 版錯 | 未初始化變數、越界、共用變數沒 `volatile` | 先開 `-Wall -Wextra` 看警告 |

### 6.3 評測回答錯或太慢

| 評測說 | 做法 |
|---|---|
| 編譯失敗 | [prompts.md](prompts.md) C 或 H1；先看第一個錯誤 |
| 答案錯 | 用 `bench_same_u32` 對原版；測邊界：長度 0、1、不是 4 的倍數、最大值、全 0、全 1、負數；查溢位（加總用 `int64_t`） |
| 執行錯誤 / 逾時無輸出 | 6.1、6.2；先把優化拿掉一半，找出是哪一步弄壞的 |
| 太慢 | [methodology.md](methodology.md) 第 0 節；[prompts.md](prompts.md) D 找瓶頸 |
| 輸出格式錯 | 換行、空白、大小寫；Python 的 `print(..., flush=True)` |
