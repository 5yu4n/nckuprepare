# 考場板子：RP2350B 機器人控制板

> 依 2026-11-14 早上拍的板子照片整理。**腳位號碼除了 IO 排針印的 40–44 以外都不知道**，程式裡一律寫成 `#define`，拿到原理圖或用掃描程式找到後再填。
> 現成專案：[templates/board-app/](templates/board-app/)（`board_pins.h` 改腳位、`board.h` 是各周邊驅動、`main.c` 有示範與兩個掃腳模式）。已用 pico-sdk 2.3.1 在 Arm 編譯連結出 `.uf2`，RISC-V 編譯通過（這台機器沒法連結 RISC-V）。**還沒上板跑過。**

## 0. 先記這五件事

1. **晶片是 RP2350B（80 腳），有 GPIO 0–47。** 用 `PICO_BOARD=pico2` 會被當成 RP2350A（只有 0–29），GPIO 30–47 全部不能用。改用範本的 `boards/exam_rp2350b.h`（裡面 `#define PICO_RP2350A 0`），或考場若給了板子的 board header 就用它。
2. **ADC 只在 GPIO 40–47**（`ADC_BASE_PIN` = 40，通道 = gpio − 40）。板上 IO 排針 40–44 剛好都能讀類比。
3. **PIO 一次只能看到 32 支腳**：0–31 或 16–47（`pio_set_gpio_base`）。用 `pio_claim_free_sm_and_add_program_for_gpio_range(..., true)` 會自動選好，不要自己 `pio_add_program` + 手選 SM。
4. **燒錄**：按住 BOOT 再插 USB（或按住 BOOT 按 RESET）→ 電腦出現 RP2350 磁碟 → 拖 `.uf2`。板上 RESET 在左下角，BOOT 很可能是左上角那顆小白鍵（猜的）。
5. **printf 看哪裡**：板上有兩個 USB-C。標「UART 0」的那個很可能是 USB 轉 UART 晶片接到 UART0；另一個（USB1）是 RP2350 原生 USB。範本兩個都開（`pico_enable_stdio_usb` + `pico_enable_stdio_uart`），哪個有字就用哪個。

## 1. 板上有什麼（照片辨識）

| 元件 | 板上標示 | 判斷 | 用什麼驅動 |
|---|---|---|---|
| 主晶片 | U15，印 RP2350B0A4 | RP2350B，A4 步進（印字看得出 B 和 A4，其他是推測） | Arm 或 RISC-V 都行 |
| 外部 flash | 晶片旁 8 腳 | 推測 QSPI flash，大小未知 | 不用管 |
| 6 顆 RGB LED | LED1、LED11、LED12、LED2、LED3、LED4 | 5050 白色方形、可定址（WS2812 類，猜的） | PIO `ws2812.pio` |
| 蜂鳴器 | BUZZER | 大顆黑色圓形，有源或無源不確定 | PWM 方波（無源）或 GPIO（有源） |
| 按鍵 | KEY1（SW3）、KEY2（SW2） | 一般按鍵，推測按下接地 | GPIO + 上拉 + 去彈跳 |
| 馬達 M1–M4 | 6 腳：OUT1 GND B A 5V OUT2 | 直流減速馬達 + 霍爾編碼器 A/B | 驅動晶片 U3–U6（8 腳 SOP，型號看不清）＋ PIO 編碼器 |
| 3 腳排針 | 左上 J6/J7/J8 印 IO 5V GND；右上 J2/J4/J5 三排印 40–44 / IO 5V GND | 伺服或感測器 | 50 Hz PWM、GPIO、ADC |
| I2C0、I2C1 | SCL SDA 5V GND | 外接感測器（可能 IMU、OLED、距離感測） | `hardware_i2c` |
| UART0 | RX TX 5V GND | 外接序列裝置 | `hardware_uart` |
| LCD 排針 J3 | GND 5V SCL SDA RST DC CS BL | **SPI** 彩色 TFT（ST7789/ST7735 類，猜的）。SCL = SCK、SDA = MOSI，不是 I2C | `hardware_spi` |
| 無線模組 | 左側銀色屏蔽罩、有 CE 標 | 型號看不清；可能是 ESP 類（走 UART/AT 指令），也可能是 CYW43 類 | 見 §10 |
| 小 QFN | U11（UART 0 USB-C 旁）、U7（電源區） | U11 推測 USB 轉 UART；U7 推測降壓 IC | 不用管 |
| 電源 | XT60、POWER_SW、3R3 電感、U1 穩壓 | 電池 → 降壓 → 5V/3.3V；紅色接頭輸出 5V 與 VOUT | 電池電壓可能接到某支 ADC（要查） |
| SWD | J1：CLK GND DIO | 除錯器 | openocd / picoprobe |

## 2. 找腳位（沒原理圖時）

順序：
1. **先找文件**：考場 GitLab / 題目 README / 範例程式裡的 `#define`、board header。看到就直接抄進 `board_pins.h`。
2. **輸入類（按鍵、編碼器）**：`cmake -DMODE=1 ..` 編譯燒錄，序列埠會印 `GPIOxx -> 0/1`。按 KEY1、KEY2，用手轉馬達輪子，看哪支腳在跳。編碼器兩支腳應該相鄰。
3. **WS2812**：`cmake -DMODE=2 ..`。程式逐腳送綠色資料，序列埠印目前腳號，LED 亮的那一刻就是那支。**先拔馬達、伺服**，因為它會把每支腳輪流當輸出。
4. **馬達 IN1/IN2、蜂鳴器**：只剩輸出類。從原理圖或範例找最快；真的沒有就一次試一支：`gpio_init` → 輸出 1 → 聽蜂鳴器 / 看輪子（輪子架空）。
5. **I2C**：示範程式開機會掃兩條 I2C 並印出位址。掃不到 → SDA/SCL 腳錯了或 I2C0/I2C1 搞反。RP2350 規則：I2C0 SDA 在 gpio%4==0、SCL 在 %4==1；I2C1 SDA 在 %4==2、SCL 在 %4==3。
6. **UART**：一般 UART 功能 TX 在 gpio%4==0、RX 在 %4==1；RP2350 另外讓 %4==2 當 TX、%4==3 當 RX（UART_AUX 功能）。uart0 或 uart1：實例 = ((gpio+4)>>3)&1，也就是 0–3、12–19、28–35、44–47 是 uart0，4–11、20–27、36–43 是 uart1（30 以上是照規律推算）。設功能一律用 `UART_FUNCSEL_NUM(uart, gpio)`，選錯實例就沒輸出。

## 3. CMake（RP2350B 關鍵三行）

```cmake
set(PICO_BOARD_HEADER_DIRS ${CMAKE_CURRENT_LIST_DIR}/boards)   # 放 exam_rp2350b.h 的資料夾
set(PICO_BOARD exam_rp2350b)                                    # 要在 include(pico_sdk_import.cmake) 之前
# RISC-V：cmake -DPICO_PLATFORM=rp2350-riscv ..   Arm：預設 rp2350-arm-s
...
pico_generate_pio_header(app ${CMAKE_CURRENT_LIST_DIR}/ws2812.pio)
pico_generate_pio_header(app ${CMAKE_CURRENT_LIST_DIR}/quadrature_encoder.pio)
target_link_libraries(app PRIVATE pico_stdlib hardware_pio hardware_pwm hardware_i2c
                      hardware_spi hardware_adc hardware_uart hardware_clocks)
pico_enable_stdio_usb(app 1)
pico_enable_stdio_uart(app 1)
```

換了 `PICO_BOARD` 或 `PICO_PLATFORM` 一定要**刪掉 build 資料夾重跑 cmake**，不然沿用舊設定。

確認有沒有真的變成 RP2350B：開機印 `NUM_BANK0_GPIOS`，要是 48。

## 4. RGB LED（WS2812，PIO）

`ws2812.pio` 從 pico-examples 抄（範本裡已有）。資料是 GRB 順序，左移 8 位元後送進 FIFO。

```c
#include "ws2812.pio.h"
static PIO led_pio; static uint led_sm, led_off;
void leds_init(void) {
    bool ok = pio_claim_free_sm_and_add_program_for_gpio_range(
        &ws2812_program, &led_pio, &led_sm, &led_off, PIN_WS2812, 1, true);
    hard_assert(ok);
    ws2812_program_init(led_pio, led_sm, led_off, PIN_WS2812, 800000, false);  // false = RGB 不是 RGBW
}
static inline void put_pixel(uint8_t r, uint8_t g, uint8_t b) {
    pio_sm_put_blocking(led_pio, led_sm, (((uint32_t)g << 16) | ((uint32_t)r << 8) | b) << 8u);
}
// 6 顆：連續 put 6 次，第 1 次是第 1 顆。送完等 >50 us 才鎖存。
```

- 顏色不對（紅綠對調）→ 這批 LED 是 RGB 順序，把 r、g 對調。
- 只亮第一顆或亂閃 → 頻率參數要 800000；LED 的 5V 資料門檻可能要 3.3V 準位轉換（板子應該做好了）。
- 亮度：WS2812 全亮很刺眼又耗電，用 1/4 亮度測試。
- LED 在板上的順序不一定是 LED1→LED4，送 `i` 號紅、其他暗，就知道對應。

## 5. 蜂鳴器（PWM）

**無源**（要方波才響）：頻率 = clk_sys / (div × (wrap+1))，佔空比 50%。
```c
void buzzer_tone(uint32_t f) {                 // f = 0 靜音
    uint slice = pwm_gpio_to_slice_num(PIN_BUZZER);
    if (!f) { pwm_set_gpio_level(PIN_BUZZER, 0); return; }
    uint32_t sys = clock_get_hz(clk_sys);
    float div = (float)sys / (f * 65536.f); if (div < 1.f) div = 1.f;
    uint32_t wrap = (uint32_t)(sys / (div * f)) - 1; if (wrap > 65535) wrap = 65535;
    pwm_set_clkdiv(slice, div); pwm_set_wrap(slice, wrap);
    pwm_set_gpio_level(PIN_BUZZER, wrap / 2);
}
// 初始化：gpio_set_function(PIN_BUZZER, GPIO_FUNC_PWM); pwm_set_enabled(slice, true);
```
**有源**（給電就響、音高固定）：`gpio_put(PIN_BUZZER, 1)`。分辨法：給 1 持續響就是有源；只「喀」一聲就是無源。

不卡主迴圈的旋律：`melody_start(notes, n)` 一次，主迴圈每圈 `melody_poll()`（`board.h`，`note_t` 是 `{hz, ms}`，hz = 0 休止）。

音符頻率（Hz）：C4 262、D4 294、E4 330、F4 349、G4 392、A4 440、B4 494、C5 523。

## 6. 按鍵（去彈跳、邊緣偵測）

```c
gpio_init(PIN_KEY1); gpio_set_dir(PIN_KEY1, GPIO_IN); gpio_pull_up(PIN_KEY1);   // 按下 = 0
// 每 1 ms 呼叫：連續 20 ms 狀態一樣才算數；回傳 true 只在「剛按下」那一次
bool key_pressed_edge(int i);   // 見 board.h
```
- 用中斷：`gpio_set_irq_enabled_with_callback(pin, GPIO_IRQ_EDGE_FALL, true, &cb);` 中斷裡只設旗標，去彈跳還是要在主迴圈做。整支程式只能有一個 GPIO callback（`gpio_set_irq_enabled_with_callback` 每次都會換掉），多支腳在 callback 裡用 `gpio` 參數分辨。
- 長按：記錄按下時間 `to_ms_since_boot(get_absolute_time())`，放開時算差值。

## 7. 直流馬達（H 橋，兩支 PWM）

板上 4 個 8 腳驅動晶片（U3–U6），連接器有 OUT1/OUT2，判斷是「IN1/IN2 兩輸入」型（AT8236、RZ7899、DRV8871 這一類，型號看不清，**是推測**）。這類晶片的真值表：

| IN1 | IN2 | 結果 |
|---|---|---|
| PWM | 0 | 正轉，速度 ∝ 佔空比 |
| 0 | PWM | 反轉 |
| 0 | 0 | 滑行（停） |
| 1 | 1 | 煞車 |

```c
#define MOTOR_PWM_WRAP 7499                       // 150 MHz / 7500 = 20 kHz，聽不到嘯叫
void motor_pin_init(uint pin) {
    gpio_set_function(pin, GPIO_FUNC_PWM);
    uint s = pwm_gpio_to_slice_num(pin);
    pwm_set_clkdiv(s, 1.f); pwm_set_wrap(s, MOTOR_PWM_WRAP); pwm_set_gpio_level(pin, 0);
    pwm_set_enabled(s, true);
}
void motor_set(int m, int speed /* -1000..1000 */);   // 見 board.h
```
- 方向反了：IN1/IN2 對調或 speed 取負，左右輪通常一邊要反。
- 低速不轉：直流馬達有啟動死區，`|speed| < 150` 左右可能不動，可以加 `speed = sign * (deadband + |speed| * (1000 - deadband) / 1000)`。
- **測試時把車架空**，免得衝下桌。
- PWM 規則：一個 slice 兩支腳（偶數 = A 通道、奇數 = B 通道），共用頻率。RP2350 有 12 個 slice，GPIO 32–47 用 slice 8–11。
- 若晶片其實是「DIR + PWM」型（一支方向、一支速度）：DIR 腳用 `gpio_put`，PWM 腳設佔空比。

## 8. 編碼器（PIO 四倍頻計數）

`quadrature_encoder.pio`（pico-examples，範本已有）要求：A、B **相鄰**（B = A+1），程式必須載在 **offset 0**，幾乎佔滿整個 PIO 的指令記憶體。所以：**4 個編碼器共用一個 PIO 的 4 個 SM，WS2812 放另一個 PIO**（RP2350 有 3 個 PIO）。

```c
#include "quadrature_encoder.pio.h"
static PIO enc_pio; static uint enc_sm[4];
void encoders_init(void) {
    uint off;
    bool ok = pio_claim_free_sm_and_add_program_for_gpio_range(
        &quadrature_encoder_program, &enc_pio, &enc_sm[0], &off, PIN_ENC1_A, 2, true);
    hard_assert(ok && off == 0);
    for (int i = 1; i < 4; i++) enc_sm[i] = pio_claim_unused_sm(enc_pio, true);
    quadrature_encoder_program_init(enc_pio, enc_sm[0], PIN_ENC1_A, 0);   // 0 = 全速取樣
    // ... 其他三個同樣 init
}
int32_t c = quadrature_encoder_get_count(enc_pio, enc_sm[0]);   // 累計計數（有正負）
```
- 4 組腳都要在同一個 32 腳窗口（0–31 或 16–47），不然要拆兩個 PIO（每個 PIO 都要各載一次程式）。
- A、B 不相鄰 → 用 `board.h` 的 `encoders_init_irq()`（GPIO 中斷版，雙邊緣，四倍頻）。
- 計數方向反了 → 取負號。
- 速度：每 T ms 取一次差值，`rpm = delta * 60000 / (T * CPR * 4 * 減速比)`。CPR（每圈脈衝數）要看馬達規格；不知道就手轉一圈看計數。

### 速度閉迴路（PI 控制，固定週期）

```c
// 每 10 ms 跑一次（repeating_timer 或主迴圈計時）
int32_t prev[4]; float integ[4];
void speed_loop(int m, float target_cnt_per_10ms) {
    int32_t now = quadrature_encoder_get_count(enc_pio, enc_sm[m]);
    float meas = (float)(now - prev[m]); prev[m] = now;
    float err = target_cnt_per_10ms - meas;
    integ[m] += err; if (integ[m] > 2000) integ[m] = 2000; if (integ[m] < -2000) integ[m] = -2000;  // 防積分飽和
    float out = 8.0f * err + 0.5f * integ[m];      // Kp、Ki 先小後大調
    motor_set(m, (int)out);
}
```
```c
static bool tick(struct repeating_timer *t) { (void)t; for (int m = 0; m < 4; m++) speed_loop(m, target[m]); return true; }
struct repeating_timer rt; add_repeating_timer_ms(-10, tick, NULL, &rt);   // 負號 = 以開始時間算間隔
```
調參：先 Ki = 0，Kp 加到開始震盪再減半，然後慢慢加 Ki 消除穩態誤差。

## 9. 伺服（50 Hz）

```c
void servo_init(uint pin) {
    gpio_set_function(pin, GPIO_FUNC_PWM);
    uint s = pwm_gpio_to_slice_num(pin);
    pwm_set_clkdiv(s, clock_get_hz(clk_sys) / 1000000.f);   // 1 MHz → level 單位是微秒
    pwm_set_wrap(s, 19999);                                  // 20 ms
    pwm_set_gpio_level(pin, 1500);                           // 置中
    pwm_set_enabled(s, true);
}
// 0°~180° ≈ 500~2500 us（多數伺服 1000~2000 us 比較安全）
```
同一個 slice 的兩支腳不能一支當伺服（50 Hz）、一支當馬達（20 kHz）。開機呼叫 `pwm_print_slices()` 會印出每支 PWM 腳的 slice，一眼看出有沒有撞到。

## 10. I2C（感測器、IMU、OLED）

```c
i2c_init(i2c0, 400 * 1000);
gpio_set_function(PIN_I2C0_SDA, GPIO_FUNC_I2C); gpio_set_function(PIN_I2C0_SCL, GPIO_FUNC_I2C);
gpio_pull_up(PIN_I2C0_SDA); gpio_pull_up(PIN_I2C0_SCL);
// 讀暫存器：先寫暫存器位址（nostop=true），再讀
uint8_t reg = 0x75, v;
i2c_write_timeout_us(i2c0, addr, &reg, 1, true, 5000);
i2c_read_timeout_us(i2c0, addr, &v, 1, false, 5000);
```
- 一律用 `_timeout_us` 版本：線沒接或位址錯時 `_blocking` 版可能卡住。回傳負數 = 失敗（`PICO_ERROR_GENERIC` = 沒 ACK、`PICO_ERROR_TIMEOUT`）。
- 先掃描（`i2c_scan`）知道有哪些位址。常見位址（記憶，以 datasheet 為準）：MPU6050 0x68/0x69、WHO_AM_I 在 0x75 → 0x68；ICM-42688 0x68/0x69、0x75 → 0x47；LSM6DS3/DSO 0x6A/0x6B、0x0F → 0x69/0x6C；QMI8658 0x6A/0x6B、0x00 → 0x05；SSD1306 OLED 0x3C；VL53L0X 0x29。
- pico-examples 有現成驅動可抄：`i2c/mpu6050_i2c`、`i2c/ssd1306_i2c`、`i2c/bus_scan`。
- 16 位元感測值通常是大端：`int16_t x = (int16_t)(buf[0] << 8 | buf[1]);`（ST 的晶片多半是小端，看 datasheet）。

## 11. SPI LCD（J3：SCL SDA RST DC CS BL）

板上印 SCL/SDA 但旁邊有 DC、CS、RST，所以是 **SPI**：SCL = SCK、SDA = MOSI，沒有 MISO。DC = 0 送命令、1 送資料；BL = 背光（拉高亮）。

```c
spi_init(spi0, 40 * 1000 * 1000);
spi_set_format(spi0, 8, SPI_CPOL_1, SPI_CPHA_1, SPI_MSB_FIRST);  // 不亮就改 CPOL_0/CPHA_0
gpio_set_function(PIN_LCD_SCK, GPIO_FUNC_SPI); gpio_set_function(PIN_LCD_MOSI, GPIO_FUNC_SPI);
// CS/DC/RST/BL 當一般 GPIO 輸出；RST 拉低 20 ms 再拉高、等 120 ms
// ST7789 最小初始化：0x01(等150ms) 0x11(等120ms) 0x3A=0x55 0x36=0x00 0x21 0x29
// 畫圖：0x2A 設欄、0x2B 設列、0x2C 後連續送 RGB565（高位元組先）
```
- 全白 / 全黑：背光沒開、DC 腳錯、SPI 模式錯、RST 沒拉高。
- 顏色反相：`0x21`（INVON）改 `0x20`。
- 畫面偏移（240×240 的 ST7789 用 240×320 控制器）：MADCTL 旋轉後 y 要加 80。
- SPI 腳位規則：SCK 在 gpio%4==2、TX(MOSI) 在 %4==3、RX 在 %4==0、CSn 在 %4==1；每 8 支一組輪流 spi0/spi1（0–7 spi0、8–15 spi1、16–23 spi0、24–31 spi1、32–39 spi0、40–47 spi1）。
- **DMA 版**（`board.h`）：`lcd_fill_dma(x, y, w, h, color)` 填色、`lcd_blit_dma(x, y, w, h, buf)` 貼一塊 RGB565 圖。做法是 SPI 暫時切 16 位元、DMA 用 `spi_get_dreq(LCD_SPI, true)` 節拍寫 `&spi_get_hw(LCD_SPI)->dr`，填色時讀位址不遞增。傳完要等 `spi_is_busy` 變 false 再拉高 CS，再把 SPI 切回 8 位元。先用 `lcd_fill` 確認畫面正常，再換 DMA 版。

## 12. UART（UART0 連接器、無線模組）

```c
uart_init(uart0, 115200);
gpio_set_function(PIN_UART0_TX, UART_FUNCSEL_NUM(uart0, PIN_UART0_TX));   // RP2350 有些腳是 UART_AUX
gpio_set_function(PIN_UART0_RX, UART_FUNCSEL_NUM(uart0, PIN_UART0_RX));
uart_puts(uart0, "hello\r\n");
while (uart_is_readable(uart0)) { char c = uart_getc(uart0); /* ... */ }
```
- 如果 printf 也走 UART0，外接裝置和 printf 會混在一起，改用 USB stdio 印除錯訊息。
- TX 接對方 RX、RX 接對方 TX；板上連接器標的 RX/TX 是「板子這邊」還是「對方那邊」要試。
- **無線模組**：型號看不清。若是 ESP 類（ESP32-C3 等）而且燒了 AT 韌體：送 `AT\r\n` 應回 `OK`，常用 `AT+CWMODE=1`、`AT+CWJAP="ssid","pwd"`、`AT+CIPSTART="TCP","ip",port`、`AT+CIPSEND=n`。若是 CYW43 類（Pico 2 W 那種）：要用 `pico_cyw43_arch_*` 函式庫並且 board header 要設 CYW43 腳位，沒原理圖很難，**考場不太可能考，有時間再碰**。

## 13. ADC（電池電壓、類比感測器）

```c
adc_init();
adc_gpio_init(44);                 // 只能 40–47
adc_select_input(44 - 40);         // = gpio - ADC_BASE_PIN
uint16_t raw = adc_read();         // 0–4095 對 0–3.3 V
float v = raw * 3.3f / 4096 * 分壓比;
// 晶片溫度：adc_set_temp_sensor_enabled(true); adc_select_input(ADC_TEMPERATURE_CHANNEL_NUM);  // RP2350B 是 8
```
雜訊大就連讀 16 次取平均。

## 14. 多核、計時、即時性

- **固定週期控制**用 `add_repeating_timer_ms(-10, cb, NULL, &t)`，callback 在中斷裡跑，要短；不要在裡面 printf。
- 重的運算（影像、路徑規劃、題目的計算部分）丟 core1：`multicore_launch_core1(fn)`，用 FIFO 或無鎖佇列交換資料（見 [templates/pico-app/dual_core.h](templates/pico-app/dual_core.h)、`spsc_ring.h`）。
- 量時間：`time_us_64()`；兩次差值就是微秒。
- `sleep_ms` 會讓整個 core 停下，主迴圈要同時顧按鍵、LED、馬達時改用 `absolute_time_t` + `time_reached()` 的非阻塞寫法（`main.c` 有示範）。

## 15. 在這塊板上「優化系統效能」先看哪裡

題目若是「讓機器人反應更快 / 更新更順 / 控制更穩」，照這個順序查（每改一項都量 `time_us_64()` 前後差）：

| 慢在哪 | 怎麼看出來 | 改法 |
|---|---|---|
| 主迴圈裡 `sleep_ms` | 按鍵反應慢、LED 卡頓 | `absolute_time_t` + `time_reached()` 非阻塞；固定週期工作改 `add_repeating_timer_ms` |
| 主迴圈或中斷裡 `printf` | 一行 printf 在 USB/UART 上要幾百微秒 | 只在 200 ms 一次的地方印；中斷裡完全不印 |
| LCD 一個像素一個 `spi_write_blocking` | 全螢幕填色要很久 | `lcd_fill_dma` / `lcd_blit_dma`；只重畫變動的區塊；SPI 時脈拉高（ST7789 多半 40–62.5 MHz 可） |
| I2C 用 100 kHz、一次讀一個暫存器 | 讀 IMU 佔掉控制週期 | `i2c_init(..., 400000)`；連續暫存器一次 burst 讀（多數感測器位址會自動遞增） |
| 編碼器用 GPIO 中斷 | 高轉速時 CPU 被中斷吃光、計數漏掉 | 改 PIO 版 `quadrature_encoder`（硬體計數，不吃 CPU） |
| 控制迴圈用 float | RISC-V 核心沒有 FPU，float 是軟體算 | 定點整數（[templates/pico-app/fixed.h](templates/pico-app/fixed.h)）；Arm 核心有 FPU，float 可以 |
| 一顆核心做所有事 | 計算重時控制週期不穩 | core0 控制（馬達、編碼器、按鍵），core1 計算或畫面；用 FIFO / `spsc_ring.h` 傳資料 |
| WS2812 每圈都更新 | 6 顆約 0.2 ms，頻繁更新也浪費 | 有變才 `leds_show()`；量大時改 DMA 餵 PIO（見 cheatsheet DMA→PIO） |
| 等 DMA 傳完才做下一件事 | CPU 閒著 | 啟動 DMA 後先做別的，最後才 `dma_channel_wait_for_finish_blocking` |

## 16. 常見坑（這塊板）

| 症狀 | 原因 |
|---|---|
| `gpio_init(40)` 之後沒反應或斷言失敗 | 用了 `PICO_BOARD=pico2`，SDK 以為只有 30 支腳 |
| PIO 在 GPIO 32 以上沒輸出 | 沒設 gpio base；改用 `pio_claim_free_sm_and_add_program_for_gpio_range(..., true)` |
| 編碼器第二個以後初始化失敗 | 每次都 claim+add program：程式佔滿 PIO，第二次加不進去。只 add 一次，其他用 `pio_claim_unused_sm` |
| 伺服抖、馬達異音 | 伺服和馬達在同一個 PWM slice，頻率互相蓋掉 |
| 馬達不轉但 LED 正常 | 電池沒接或 POWER_SW 沒開（USB 5V 通常推不動馬達） |
| USB 序列埠一直斷 | 馬達啟動電流讓電壓掉；用電池供電 |
| I2C 掃不到 | SDA/SCL 腳錯、I2C0/1 搞反、模組沒供電、`gpio_pull_up` 沒加 |
| LCD 白屏 | BL 沒拉高、RST 一直低、DC 腳錯 |
| 按鍵一下算好幾次 | 沒去彈跳 |
