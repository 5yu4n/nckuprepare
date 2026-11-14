// 考場板子示範程式：先用 MODE_SCAN_* 找腳位，填進 board_pins.h，再用 MODE_DEMO 測每個周邊。
// 編譯：mkdir build && cd build && cmake .. && make -j   （RISC-V：cmake -DPICO_PLATFORM=rp2350-riscv ..）
// 燒錄：按住 BOOT 插 USB（或按住 BOOT 按 RESET）→ 出現 RP2350 磁碟 → 拖 board_app.uf2
#include "board_pins.h"
#include "board.h"

#define MODE_DEMO         0
#define MODE_SCAN_IN      1   // 印出所有 GPIO 輸入變化：按 KEY、轉馬達輪子，看哪支腳在變
#define MODE_SCAN_WS2812  2   // 逐腳送 WS2812 綠色，序列埠印目前腳號，哪支讓 LED 亮就是它
#ifndef MODE
#define MODE MODE_DEMO
#endif

#if MODE == MODE_SCAN_IN
static void scan_inputs(void) {
    // 跳過 QSPI flash 腳（不在 bank0）；USB 不在 bank0。只讀不寫，安全。
    uint64_t prev = 0;
    for (uint p = 0; p < NUM_BANK0_GPIOS; p++) { gpio_init(p); gpio_set_dir(p, GPIO_IN); gpio_pull_up(p); }
    while (true) {
        uint64_t now = 0;
        for (uint p = 0; p < NUM_BANK0_GPIOS; p++) now |= (uint64_t)gpio_get(p) << p;
        uint64_t diff = now ^ prev;
        for (uint p = 0; p < NUM_BANK0_GPIOS; p++)
            if (diff >> p & 1) printf("GPIO%u -> %u\n", p, (unsigned)(now >> p & 1));
        prev = now;
        sleep_ms(5);
    }
}
#endif

#if MODE == MODE_SCAN_WS2812
static void scan_ws2812(void) {
    // 注意：這會把每支腳輪流當輸出。先拔掉馬達、伺服，避免誤驅動。跳過 UART0 腳以免序列埠斷線。
    for (uint p = 0; p < NUM_BANK0_GPIOS; p++) {
        if (p == PICO_DEFAULT_UART_TX_PIN || p == PICO_DEFAULT_UART_RX_PIN) continue;
        PIO pio; uint sm, off;
        if (!pio_claim_free_sm_and_add_program_for_gpio_range(&ws2812_program, &pio, &sm, &off, p, 1, true)) continue;
        ws2812_program_init(pio, sm, off, p, 800000, false);
        printf("try GPIO%u\n", p);
        for (int k = 0; k < 6; k++) pio_sm_put_blocking(pio, sm, 0x100000u << 8u);   // 暗綠
        sleep_ms(1500);
        for (int k = 0; k < 6; k++) pio_sm_put_blocking(pio, sm, 0);
        sleep_ms(100);
        pio_remove_program_and_unclaim_sm(&ws2812_program, pio, sm, off);
        gpio_init(p);                                                       // 放回輸入
    }
    printf("scan done\n");
}
#endif

int main(void) {
    stdio_init_all();
    sleep_ms(1500);                                     // 等 USB 序列埠
    printf("board_app start, NUM_BANK0_GPIOS=%u, sys=%lu Hz\n", (unsigned)NUM_BANK0_GPIOS,
           (unsigned long)clock_get_hz(clk_sys));
#if MODE == MODE_SCAN_IN
    scan_inputs();
#elif MODE == MODE_SCAN_WS2812
    while (true) scan_ws2812();
#else
    leds_init();
    buzzer_init();
    keys_init();
    motors_init();
    encoders_init();
    servo_init(PIN_SERVO1);
    i2c_bus_init(i2c0, PIN_I2C0_SDA, PIN_I2C0_SCL, 400000);
    i2c_bus_init(i2c1, PIN_I2C1_SDA, PIN_I2C1_SCL, 400000);
    i2c_scan(i2c0); i2c_scan(i2c1);
    vbat_init();
    buzzer_beep(2000, 100);

    int speed = 0, hue = 0;
    absolute_time_t next_print = make_timeout_time_ms(200);
    while (true) {
        if (key_pressed_edge(0)) { speed += 250; if (speed > 1000) speed = -1000; buzzer_beep(1500, 30); }
        if (key_pressed_edge(1)) { speed = 0; buzzer_beep(800, 30); }
        for (int m = 0; m < 4; m++) motor_set(m, speed);
        servo_deg(PIN_SERVO1, (speed + 1000) * 180 / 2000);

        for (int i = 0; i < NUM_LEDS; i++) {
            int h = (hue + i * 40) % 240;               // 粗略色環
            uint8_t r = h < 80 ? 80 - h : (h >= 160 ? h - 160 : 0);
            uint8_t g = h < 80 ? h : (h < 160 ? 160 - h : 0);
            uint8_t b = h >= 80 && h < 160 ? h - 80 : (h >= 160 ? 240 - h : 0);
            leds_set(i, r / 4, g / 4, b / 4);
        }
        if (++hue >= 240) hue = 0;

        if (time_reached(next_print)) {
            leds_show();
            next_print = make_timeout_time_ms(200);
            printf("spd=%d enc=%ld %ld %ld %ld vbat=%.2f\n", speed,
                   (long)encoder_get(0), (long)encoder_get(1), (long)encoder_get(2), (long)encoder_get(3),
                   (double)vbat_read());
        }
        sleep_ms(1);                                    // 1 ms 節拍給按鍵去彈跳
    }
#endif
}
