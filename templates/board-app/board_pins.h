// board_pins.h — 所有腳位集中在這裡。數字都是「待確認」的佔位值（只有 IO 排針 40–44 是板上印的）。
// 找腳位：1) 考場 GitLab / README / 原理圖  2) main.c 的 MODE_SCAN_IN（按鍵、編碼器）
//         3) MODE_SCAN_WS2812（逐腳送 LED 資料，看哪支亮）。
#pragma once

// ---- 6 顆可定址 RGB LED（WS2812 類，串成一條）----
#define PIN_WS2812      22      // ? 資料腳
#define NUM_LEDS        6

// ---- 蜂鳴器（板上大顆黑色圓形 BUZZER）----
#define PIN_BUZZER      23      // ?
#define BUZZER_PASSIVE  1       // 1 = 無源（要 PWM 方波），0 = 有源（給 1 就響）

// ---- 按鍵 KEY1 / KEY2（SW3 / SW2），假設按下 = 低電位 ----
#define PIN_KEY1        24      // ?
#define PIN_KEY2        25      // ?
#define KEY_ACTIVE_LOW  1

// ---- 4 個馬達 M1–M4：每個驅動晶片（U3–U6，8 腳）兩個輸入 IN1/IN2 ----
// 連接器腳序：OUT1 GND B A 5V OUT2（OUT1/OUT2 = 馬達，A/B = 編碼器，5V/GND = 編碼器電源）
#define PIN_M1_IN1      2       // ?
#define PIN_M1_IN2      3       // ?
#define PIN_M2_IN1      4       // ?
#define PIN_M2_IN2      5       // ?
#define PIN_M3_IN1      6       // ?
#define PIN_M3_IN2      7       // ?
#define PIN_M4_IN1      8       // ?
#define PIN_M4_IN2      9       // ?

// ---- 編碼器 A/B。PIO 版要求 B = A + 1（相鄰腳）----
#define PIN_ENC1_A      10      // ?
#define PIN_ENC2_A      12      // ?
#define PIN_ENC3_A      14      // ?
#define PIN_ENC4_A      16      // ?

// ---- 伺服 / 3 腳排針（IO 5V GND）。板上 IO 排針印 40–44 ----
#define PIN_SERVO1      40
#define PIN_SERVO2      41

// ---- I2C0 / I2C1 連接器（SCL SDA 5V GND）。RP2350：I2C0 SDA 在 GPIO%4==0，SCL 在 %4==1；I2C1 是 %4==2 / %4==3 ----
#define PIN_I2C0_SDA    20      // ?
#define PIN_I2C0_SCL    21      // ?
#define PIN_I2C1_SDA    26      // ?
#define PIN_I2C1_SCL    27      // ?

// ---- UART0 連接器（RX TX 5V GND）、無線模組的 UART（若是 ESP 類 AT 模組）----
#define PIN_UART0_TX    0       // ?
#define PIN_UART0_RX    1       // ?
#define PIN_UART1_TX    36      // ? 無線模組
#define PIN_UART1_RX    37      // ?

// ---- LCD 排針 J3：GND 5V SCL SDA RST DC CS BL（SCL=SPI SCK、SDA=SPI MOSI，不是 I2C！）----
#define LCD_SPI         spi0
#define PIN_LCD_SCK     18      // ? 板上印 SCL
#define PIN_LCD_MOSI    19      // ? 板上印 SDA
#define PIN_LCD_CS      17      // ?
#define PIN_LCD_DC      28      // ?
#define PIN_LCD_RST     29      // ?
#define PIN_LCD_BL      30      // ?
#define LCD_W           240     // ? 1.3"/1.54" ST7789 常見 240x240；ST7735 128x160
#define LCD_H           240

// ---- ADC：RP2350B 只有 GPIO 40–47 能當 ADC（通道 = gpio - 40）----
#define PIN_ADC_VBAT    44      // ? 若電池分壓接在某支 ADC 腳
#define VBAT_DIVIDER    11.0f   // ? 分壓比（看原理圖電阻）
