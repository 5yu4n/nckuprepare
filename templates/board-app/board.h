// board.h — 板上各周邊的最小驅動。所有 SDK 名稱對照 pico-sdk 2.3.1 / pico-examples。
// 用法：#include "board_pins.h" 後 #include "board.h"；CMake 要 pico_generate_pio_header 兩支 .pio。
#pragma once
#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/pwm.h"
#include "hardware/clocks.h"
#include "hardware/i2c.h"
#include "hardware/spi.h"
#include "hardware/adc.h"
#include "hardware/uart.h"
#include "hardware/dma.h"
#include "ws2812.pio.h"
#include "quadrature_encoder.pio.h"

// ======================= WS2812（PIO）=======================
// GPIO >= 32 時 PIO 要把 gpio base 設成 16；pio_claim_free_sm_and_add_program_for_gpio_range 會自己處理。
static PIO  led_pio; static uint led_sm, led_off;
static uint32_t led_buf[NUM_LEDS];                 // GRB，每顆 24 bit

static inline void leds_init(void) {
    bool ok = pio_claim_free_sm_and_add_program_for_gpio_range(
        &ws2812_program, &led_pio, &led_sm, &led_off, PIN_WS2812, 1, true);
    hard_assert(ok);
    ws2812_program_init(led_pio, led_sm, led_off, PIN_WS2812, 800000, false);
}
static inline void leds_set(int i, uint8_t r, uint8_t g, uint8_t b) {
    if (i >= 0 && i < NUM_LEDS) led_buf[i] = ((uint32_t)g << 16) | ((uint32_t)r << 8) | b;
}
static inline void leds_show(void) {                // 送完全部後要 >50 us 低電位才會鎖存
    for (int i = 0; i < NUM_LEDS; i++) pio_sm_put_blocking(led_pio, led_sm, led_buf[i] << 8u);
    sleep_us(80);
}

// ======================= PWM 共用 =======================
// 每個 slice 兩個通道（A=偶數腳、B=奇數腳）共用頻率；同一 slice 的兩支腳頻率必須一樣。
static inline void pwm_pin_setup(uint pin, uint32_t freq_hz, uint16_t wrap) {
    gpio_set_function(pin, GPIO_FUNC_PWM);
    uint slice = pwm_gpio_to_slice_num(pin);
    float div = (float)clock_get_hz(clk_sys) / ((float)freq_hz * (wrap + 1));
    if (div < 1.f) div = 1.f;
    if (div > 255.f) div = 255.f;
    pwm_set_clkdiv(slice, div);
    pwm_set_wrap(slice, wrap);
    pwm_set_gpio_level(pin, 0);
    pwm_set_enabled(slice, true);
}

// ======================= 蜂鳴器 =======================
static inline void buzzer_init(void) {
#if BUZZER_PASSIVE
    pwm_pin_setup(PIN_BUZZER, 1000, 999);
#else
    gpio_init(PIN_BUZZER); gpio_set_dir(PIN_BUZZER, GPIO_OUT); gpio_put(PIN_BUZZER, 0);
#endif
}
static inline void buzzer_tone(uint32_t freq_hz) {       // 0 = 靜音
#if BUZZER_PASSIVE
    if (!freq_hz) { pwm_set_gpio_level(PIN_BUZZER, 0); return; }
    uint slice = pwm_gpio_to_slice_num(PIN_BUZZER);
    uint32_t sys = clock_get_hz(clk_sys);
    float div = (float)sys / (freq_hz * 65536.f);         // 讓 wrap 盡量大
    if (div < 1.f) div = 1.f;
    uint32_t wrap = (uint32_t)((float)sys / (div * freq_hz)) - 1;
    if (wrap > 65535) wrap = 65535;
    pwm_set_clkdiv(slice, div);
    pwm_set_wrap(slice, (uint16_t)wrap);
    pwm_set_gpio_level(PIN_BUZZER, (uint16_t)(wrap / 2));  // 50% 方波
#else
    gpio_put(PIN_BUZZER, freq_hz != 0);
#endif
}
static inline void buzzer_beep(uint32_t freq_hz, uint32_t ms) { buzzer_tone(freq_hz); sleep_ms(ms); buzzer_tone(0); }

// ======================= 按鍵（去彈跳）=======================
typedef struct { uint pin; bool stable; uint8_t cnt; } key_t_;
static key_t_ keys[2] = { { PIN_KEY1, false, 0 }, { PIN_KEY2, false, 0 } };

static inline void keys_init(void) {
    for (int i = 0; i < 2; i++) {
        gpio_init(keys[i].pin); gpio_set_dir(keys[i].pin, GPIO_IN);
#if KEY_ACTIVE_LOW
        gpio_pull_up(keys[i].pin);
#else
        gpio_pull_down(keys[i].pin);
#endif
    }
}
static inline bool key_raw(int i) { return gpio_get(keys[i].pin) ^ KEY_ACTIVE_LOW; }
// 每 1 ms 呼叫一次（主迴圈或 repeating timer）；回傳 true 表示「剛按下」這一刻。
static inline bool key_pressed_edge(int i) {
    bool r = key_raw(i);
    if (r == keys[i].stable) { keys[i].cnt = 0; return false; }
    if (++keys[i].cnt < 20) return false;                // 連續 20 ms 一樣才算
    keys[i].stable = r; keys[i].cnt = 0;
    return r;
}

// ======================= 直流馬達（IN1/IN2 兩輸入 H 橋）=======================
// 常見 8 腳驅動（AT8236、RZ7899、DRV8871 類）的真值表：IN1=PWM,IN2=0 正轉；IN1=0,IN2=PWM 反轉；
// 0/0 滑行；1/1 煞車。實際晶片型號看不清，若方向相反就把 IN1/IN2 對調或把 speed 取負。
#define MOTOR_PWM_WRAP 7499                              // 150 MHz / 7500 = 20 kHz（人耳聽不到）
static const uint8_t motor_pins[4][2] = {
    { PIN_M1_IN1, PIN_M1_IN2 }, { PIN_M2_IN1, PIN_M2_IN2 },
    { PIN_M3_IN1, PIN_M3_IN2 }, { PIN_M4_IN1, PIN_M4_IN2 } };

static inline void motors_init(void) {
    for (int m = 0; m < 4; m++)
        for (int k = 0; k < 2; k++) pwm_pin_setup(motor_pins[m][k], 20000, MOTOR_PWM_WRAP);
}
// speed: -1000..1000
static inline void motor_set(int m, int speed) {
    if (speed > 1000) speed = 1000;
    if (speed < -1000) speed = -1000;
    uint16_t lv = (uint16_t)((uint32_t)(speed < 0 ? -speed : speed) * (MOTOR_PWM_WRAP + 1) / 1000);
    pwm_set_gpio_level(motor_pins[m][0], speed > 0 ? lv : 0);
    pwm_set_gpio_level(motor_pins[m][1], speed < 0 ? lv : 0);
}
static inline void motor_brake(int m) {
    pwm_set_gpio_level(motor_pins[m][0], MOTOR_PWM_WRAP + 1);
    pwm_set_gpio_level(motor_pins[m][1], MOTOR_PWM_WRAP + 1);
}

// ======================= 編碼器（PIO，quadrature_encoder.pio）=======================
// 程式必須放在 offset 0、幾乎佔滿 32 指令 → 4 個編碼器共用一個 PIO 的 4 個 SM，WS2812 放別的 PIO。
// 4 組腳位都要在同一個 32 腳窗口內（0–31 或 16–47）。
static PIO enc_pio; static uint enc_sm[4];
static const uint8_t enc_pins[4] = { PIN_ENC1_A, PIN_ENC2_A, PIN_ENC3_A, PIN_ENC4_A };

static inline void encoders_init(void) {
    uint off;
    // 第一個：找空 PIO、設 gpio base、載入程式
    bool ok = pio_claim_free_sm_and_add_program_for_gpio_range(
        &quadrature_encoder_program, &enc_pio, &enc_sm[0], &off, enc_pins[0], 2, true);
    hard_assert(ok && off == 0);
    for (int i = 1; i < 4; i++) enc_sm[i] = (uint)pio_claim_unused_sm(enc_pio, true);
    for (int i = 0; i < 4; i++) quadrature_encoder_program_init(enc_pio, enc_sm[i], enc_pins[i], 0);
}
static inline int32_t encoder_get(int i) { return quadrature_encoder_get_count(enc_pio, enc_sm[i]); }

// ---- 備案：A/B 不相鄰或 PIO 不夠時，用 GPIO 中斷（每個邊緣 ×4 計數）----
static volatile int32_t enc_irq_count[4];
static inline void enc_irq_cb(uint gpio, uint32_t events) {
    (void)events;
    for (int i = 0; i < 4; i++) {
        uint a = enc_pins[i], b = enc_pins[i] + 1;       // 不相鄰就改這裡
        if (gpio == a || gpio == b) {
            bool A = gpio_get(a), B = gpio_get(b);
            // A 變化時 A!=B 為正轉；B 變化時 A==B 為正轉
            if (gpio == a) enc_irq_count[i] += (A != B) ? 1 : -1;
            else           enc_irq_count[i] += (A == B) ? 1 : -1;
        }
    }
}
static inline void encoders_init_irq(void) {
    for (int i = 0; i < 4; i++)
        for (uint p = enc_pins[i]; p <= enc_pins[i] + 1u; p++) {
            gpio_init(p); gpio_set_dir(p, GPIO_IN); gpio_pull_up(p);
            gpio_set_irq_enabled_with_callback(p, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true, &enc_irq_cb);
        }
}

// ======================= 伺服（50 Hz）=======================
// 時脈除到 1 MHz：level 直接等於脈寬微秒（500–2500）。
static inline void servo_init(uint pin) {
    gpio_set_function(pin, GPIO_FUNC_PWM);
    uint slice = pwm_gpio_to_slice_num(pin);
    pwm_set_clkdiv(slice, (float)clock_get_hz(clk_sys) / 1000000.f);   // 150 MHz → 150
    pwm_set_wrap(slice, 19999);                                        // 20 ms
    pwm_set_gpio_level(pin, 1500);
    pwm_set_enabled(slice, true);
}
static inline void servo_us(uint pin, uint16_t us) { pwm_set_gpio_level(pin, us); }
static inline void servo_deg(uint pin, int deg) {                     // 0..180 → 500..2500 us
    if (deg < 0) deg = 0;
    if (deg > 180) deg = 180;
    servo_us(pin, (uint16_t)(500 + deg * 2000 / 180));
}

// ======================= I2C（掃描 + 讀寫暫存器）=======================
static inline void i2c_bus_init(i2c_inst_t *i2c, uint sda, uint scl, uint baud) {
    i2c_init(i2c, baud);
    gpio_set_function(sda, GPIO_FUNC_I2C); gpio_set_function(scl, GPIO_FUNC_I2C);
    gpio_pull_up(sda); gpio_pull_up(scl);
}
static inline void i2c_scan(i2c_inst_t *i2c) {
    printf("I2C%u scan:", i2c_get_index(i2c));
    for (int a = 0x08; a < 0x78; a++) {
        uint8_t d;
        if (i2c_read_timeout_us(i2c, (uint8_t)a, &d, 1, false, 2000) >= 0) printf(" 0x%02x", a);
    }
    printf("\n");
}
static inline int i2c_reg_write(i2c_inst_t *i2c, uint8_t addr, uint8_t reg, uint8_t val) {
    uint8_t b[2] = { reg, val };
    return i2c_write_timeout_us(i2c, addr, b, 2, false, 5000);
}
static inline int i2c_reg_read(i2c_inst_t *i2c, uint8_t addr, uint8_t reg, uint8_t *dst, size_t n) {
    int r = i2c_write_timeout_us(i2c, addr, &reg, 1, true, 5000);        // repeated start
    if (r < 0) return r;
    return i2c_read_timeout_us(i2c, addr, dst, n, false, 5000);
}

// ======================= SPI LCD（ST7789 類，最小初始化）=======================
static inline void lcd_cmd(uint8_t c, const uint8_t *d, size_t n) {
    gpio_put(PIN_LCD_CS, 0);
    gpio_put(PIN_LCD_DC, 0); spi_write_blocking(LCD_SPI, &c, 1);
    if (n) { gpio_put(PIN_LCD_DC, 1); spi_write_blocking(LCD_SPI, d, n); }
    gpio_put(PIN_LCD_CS, 1);
}
static inline void lcd_init(void) {
    spi_init(LCD_SPI, 40 * 1000 * 1000);
    spi_set_format(LCD_SPI, 8, SPI_CPOL_1, SPI_CPHA_1, SPI_MSB_FIRST);   // ST7789 無 CS 模組要 mode 3；有 CS 用 mode 0 也可
    gpio_set_function(PIN_LCD_SCK, GPIO_FUNC_SPI);
    gpio_set_function(PIN_LCD_MOSI, GPIO_FUNC_SPI);
    const uint pins[] = { PIN_LCD_CS, PIN_LCD_DC, PIN_LCD_RST, PIN_LCD_BL };
    for (int i = 0; i < 4; i++) { gpio_init(pins[i]); gpio_set_dir(pins[i], GPIO_OUT); gpio_put(pins[i], 1); }
    gpio_put(PIN_LCD_RST, 0); sleep_ms(20); gpio_put(PIN_LCD_RST, 1); sleep_ms(120);
    lcd_cmd(0x01, NULL, 0); sleep_ms(150);                 // SWRESET
    lcd_cmd(0x11, NULL, 0); sleep_ms(120);                 // SLPOUT
    uint8_t c16 = 0x55; lcd_cmd(0x3A, &c16, 1);            // COLMOD 16-bit RGB565
    uint8_t mad = 0x00; lcd_cmd(0x36, &mad, 1);            // MADCTL（旋轉/RGB 順序在這改）
    lcd_cmd(0x21, NULL, 0);                                // INVON（ST7789 多半要；顏色反了就改 0x20）
    lcd_cmd(0x29, NULL, 0); sleep_ms(20);                  // DISPON
}
static inline void lcd_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
    uint8_t ca[4] = { x0 >> 8, x0 & 0xff, x1 >> 8, x1 & 0xff };
    uint8_t ra[4] = { y0 >> 8, y0 & 0xff, y1 >> 8, y1 & 0xff };
    lcd_cmd(0x2A, ca, 4); lcd_cmd(0x2B, ra, 4); lcd_cmd(0x2C, NULL, 0);
}
static inline void lcd_fill(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t rgb565) {
    lcd_window(x, y, x + w - 1, y + h - 1);
    uint8_t px[2] = { rgb565 >> 8, rgb565 & 0xff };
    gpio_put(PIN_LCD_CS, 0); gpio_put(PIN_LCD_DC, 1);
    for (uint32_t i = 0; i < (uint32_t)w * h; i++) spi_write_blocking(LCD_SPI, px, 2);
    gpio_put(PIN_LCD_CS, 1);
}
// ---- DMA 版：整塊填色 / 貼圖，CPU 不用一個一個像素送（大面積快好幾倍，CPU 可同時做別的）----
// 做法：SPI 暫時切成 16 位元，DMA 以 SPI 的 TX DREQ 節拍把像素寫進 SPI 資料暫存器。
static int lcd_dma_ch = -1;
static inline void lcd_dma_send16(const uint16_t *src, uint32_t count, bool src_inc) {
    if (lcd_dma_ch < 0) lcd_dma_ch = dma_claim_unused_channel(true);
    spi_set_format(LCD_SPI, 16, SPI_CPOL_1, SPI_CPHA_1, SPI_MSB_FIRST);    // 和 lcd_init 同模式
    gpio_put(PIN_LCD_CS, 0); gpio_put(PIN_LCD_DC, 1);
    dma_channel_config c = dma_channel_get_default_config(lcd_dma_ch);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_16);
    channel_config_set_read_increment(&c, src_inc);
    channel_config_set_write_increment(&c, false);
    channel_config_set_dreq(&c, spi_get_dreq(LCD_SPI, true));
    dma_channel_configure(lcd_dma_ch, &c, &spi_get_hw(LCD_SPI)->dr, src, count, true);
    dma_channel_wait_for_finish_blocking(lcd_dma_ch);       // 要並行就把這行和下面收尾移到之後
    while (spi_is_busy(LCD_SPI)) tight_loop_contents();     // 等最後一個像素真的送出去
    while (spi_is_readable(LCD_SPI)) (void)spi_get_hw(LCD_SPI)->dr;
    spi_get_hw(LCD_SPI)->icr = SPI_SSPICR_RORIC_BITS;       // 清 RX 溢位旗標（同 spi_write_blocking 收尾）
    gpio_put(PIN_LCD_CS, 1);
    spi_set_format(LCD_SPI, 8, SPI_CPOL_1, SPI_CPHA_1, SPI_MSB_FIRST);     // 換回 8 位元給 lcd_cmd 用
}
static inline void lcd_fill_dma(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t rgb565) {
    static uint16_t px;                                     // DMA 讀的位址要在傳完前都有效
    px = rgb565;
    lcd_window(x, y, x + w - 1, y + h - 1);
    lcd_dma_send16(&px, (uint32_t)w * h, false);            // 讀位址不遞增：同一個顏色重複送
}
// buf 是 w*h 個 RGB565（原生 uint16_t，不用自己換位元組順序，SPI 16 位元模式會先送高位元組）
static inline void lcd_blit_dma(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint16_t *buf) {
    lcd_window(x, y, x + w - 1, y + h - 1);
    lcd_dma_send16(buf, (uint32_t)w * h, true);
}

#define RGB565(r, g, b) (uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3))

// ======================= UART（UART0 連接器、無線模組）=======================
// RP2350 有些腳的 UART 在 AUX 功能；用 UART_FUNCSEL_NUM 自動選。
static inline void uart_link_init(uart_inst_t *u, uint tx, uint rx, uint baud) {
    uart_init(u, baud);
    gpio_set_function(tx, UART_FUNCSEL_NUM(u, tx));
    gpio_set_function(rx, UART_FUNCSEL_NUM(u, rx));
}
// 非阻塞讀一行（\n 結尾）；讀到整行回傳長度，否則 -1
static inline int uart_read_line(uart_inst_t *u, char *buf, int cap, int *len) {
    while (uart_is_readable(u)) {
        char c = uart_getc(u);
        if (c == '\r') continue;
        if (c == '\n') { buf[*len] = 0; int n = *len; *len = 0; return n; }
        if (*len < cap - 1) buf[(*len)++] = c;
    }
    return -1;
}

// ======================= ADC（GPIO 40–47）=======================
static inline void vbat_init(void) { adc_init(); adc_gpio_init(PIN_ADC_VBAT); }
static inline float vbat_read(void) {
    adc_select_input(PIN_ADC_VBAT - ADC_BASE_PIN);          // RP2350B: ADC_BASE_PIN = 40
    uint16_t raw = adc_read();                               // 12-bit
    return raw * 3.3f / 4096.f * VBAT_DIVIDER;
}

// ======================= 非阻塞旋律（蜂鳴器）=======================
// 主迴圈每圈呼叫 melody_poll()，不會卡住按鍵、馬達、LED。
typedef struct { uint16_t hz, ms; } note_t;                 // hz = 0 是休止符
static const note_t *mel; static int mel_n, mel_i; static absolute_time_t mel_next;
static inline void melody_start(const note_t *notes, int n) {
    mel = notes; mel_n = n; mel_i = 0; mel_next = get_absolute_time();
}
static inline void melody_poll(void) {
    if (!mel || !time_reached(mel_next)) return;
    if (mel_i >= mel_n) { buzzer_tone(0); mel = NULL; return; }
    buzzer_tone(mel[mel_i].hz);
    mel_next = make_timeout_time_ms(mel[mel_i].ms);
    mel_i++;
}

// ======================= 檢查 PWM slice 衝突 =======================
// 開機印一次：同一個 slice 的兩支腳頻率共用，伺服（50 Hz）和馬達（20 kHz）不能同 slice。
static inline void pwm_print_slices(void) {
    const struct { const char *name; uint pin; } p[] = {
        { "BUZZER", PIN_BUZZER }, { "SERVO1", PIN_SERVO1 }, { "SERVO2", PIN_SERVO2 },
        { "M1_IN1", PIN_M1_IN1 }, { "M1_IN2", PIN_M1_IN2 }, { "M2_IN1", PIN_M2_IN1 }, { "M2_IN2", PIN_M2_IN2 },
        { "M3_IN1", PIN_M3_IN1 }, { "M3_IN2", PIN_M3_IN2 }, { "M4_IN1", PIN_M4_IN1 }, { "M4_IN2", PIN_M4_IN2 } };
    for (unsigned i = 0; i < sizeof p / sizeof p[0]; i++)
        printf("%-7s GPIO%-2u slice %u %c\n", p[i].name, p[i].pin, pwm_gpio_to_slice_num(p[i].pin),
               pwm_gpio_to_channel(p[i].pin) ? 'B' : 'A');
}
