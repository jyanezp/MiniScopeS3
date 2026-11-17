
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_timer.h"
#include "esp_err.h"
#include "esp_log.h"

#define SDA_PIN    GPIO_NUM_8
#define SCL_PIN    GPIO_NUM_9
#define BTN_PIN    GPIO_NUM_0
#define ADC_CH     ADC_CHANNEL_3
#define OLED_ADDR  0x3C

#define W        128
#define H        64
#define PLOT_TOP 10
#define N        W

static const char *TAG = "osc";

static i2c_master_dev_handle_t oled;
static adc_oneshot_unit_handle_t adc;
static adc_cali_handle_t cali;
static bool cali_ok = false;

static uint8_t fb[1 + W * H / 8];
#define FB (fb + 1)

static const uint32_t TB_US[] = {100, 200, 500, 1000, 2000, 5000, 30000, 100000};
#define NTB (sizeof(TB_US) / sizeof(TB_US[0]))
static int tb = 6;

static uint16_t cap[2 * N];
static uint16_t roll_buf[N];

static void oled_cmds(const uint8_t *c, size_t n)
{
    uint8_t buf[32];
    buf[0] = 0x00;
    memcpy(buf + 1, c, n);
    ESP_ERROR_CHECK(i2c_master_transmit(oled, buf, n + 1, 100));
}

static void oled_init(void)
{
    static const uint8_t init[] = {
        0xAE, 0xD5, 0x80, 0xA8, 0x3F, 0xD3, 0x00, 0x40,
        0x8D, 0x14, 0x20, 0x00, 0xA1, 0xC8, 0xDA, 0x12,
        0x81, 0xCF, 0xD9, 0xF1, 0xDB, 0x40, 0xA4, 0xA6, 0xAF
    };
    oled_cmds(init, sizeof(init));
}

static void oled_flush(void)
{
    static const uint8_t win[] = {0x21, 0, W - 1, 0x22, 0, 7};
    oled_cmds(win, sizeof(win));
    fb[0] = 0x40;
    ESP_ERROR_CHECK(i2c_master_transmit(oled, fb, sizeof(fb), 100));
}

static inline void fb_clear(void) { memset(FB, 0, W * H / 8); }

static inline void px(int x, int y)
{
    if (x < 0 || x >= W || y < 0 || y >= H) return;
    FB[(y >> 3) * W + x] |= (uint8_t)(1 << (y & 7));
}

static void vline(int x, int y0, int y1)
{
    if (y0 > y1) { int t = y0; y0 = y1; y1 = t; }
    for (int y = y0; y <= y1; y++) px(x, y);
}

static const char FONT_CHARS[] = " .0123456789HLORVmpsuz-/";
static const uint8_t FONT[][5] = {
    {0x00,0x00,0x00,0x00,0x00}, {0x00,0x60,0x60,0x00,0x00},
    {0x3E,0x51,0x49,0x45,0x3E}, {0x00,0x42,0x7F,0x40,0x00},
    {0x42,0x61,0x51,0x49,0x46}, {0x21,0x41,0x45,0x4B,0x31},
    {0x18,0x14,0x12,0x7F,0x10}, {0x27,0x45,0x45,0x45,0x39},
    {0x3C,0x4A,0x49,0x49,0x30}, {0x01,0x71,0x09,0x05,0x03},
    {0x36,0x49,0x49,0x49,0x36}, {0x06,0x49,0x49,0x29,0x1E},
    {0x7F,0x08,0x08,0x08,0x7F}, {0x7F,0x40,0x40,0x40,0x40},
    {0x3E,0x41,0x41,0x41,0x3E}, {0x7F,0x09,0x19,0x29,0x46},
    {0x1F,0x20,0x40,0x20,0x1F}, {0x7C,0x04,0x18,0x04,0x78},
    {0x7C,0x14,0x14,0x14,0x08}, {0x48,0x54,0x54,0x54,0x20},
    {0x3C,0x40,0x40,0x20,0x7C}, {0x44,0x64,0x54,0x4C,0x44},
    {0x08,0x08,0x08,0x08,0x08}, {0x20,0x10,0x08,0x04,0x02}
};

static void draw_text(int x, const char *s)
{
    for (; *s && x <= W - 5; s++, x += 6) {
        const char *p = strchr(FONT_CHARS, *s);
        if (!p) continue;
        const uint8_t *g = FONT[p - FONT_CHARS];
        for (int i = 0; i < 5; i++) FB[x + i] |= g[i];
    }
}

static inline int to_y(uint16_t raw)
{
    return (H - 1) - (int)((uint32_t)raw * (H - 1 - PLOT_TOP) / 4095);
}

static void draw_grid(void)
{
    for (int x = 0; x < W; x += 16)
        for (int y = PLOT_TOP; y < H; y += 4) px(x, y);
    const int ys[3] = {PLOT_TOP, (PLOT_TOP + H - 1) / 2, H - 1};
    for (int k = 0; k < 3; k++)
        for (int x = 0; x < W; x += 4) px(x, ys[k]);
}

static void draw_trace(const uint16_t *s)
{
    int prev = to_y(s[0]);
    for (int x = 1; x < N; x++) {
        int y = to_y(s[x]);
        vline(x, prev, y);
        prev = y;
    }
}

static int fmt_tb(char *out, size_t n, uint32_t us_per_sample)
{
    uint32_t d = us_per_sample * 16;
    if (d >= 1000000) return snprintf(out, n, "%.1fs ", d / 1000000.0);
    if (d >= 1000)    return snprintf(out, n, "%.1fms ", d / 1000.0);
    return snprintf(out, n, "%luus ", (unsigned long)d);
}

static inline uint16_t adc_read(void)
{
    int r = 0;
    adc_oneshot_read(adc, ADC_CH, &r);
    return (uint16_t)r;
}

static int to_mv(int raw)
{
    int mv;
    if (cali_ok && adc_cali_raw_to_voltage(cali, raw, &mv) == ESP_OK) return mv;
    return raw * 3300 / 4095;
}

static void check_button(void)
{
    static int last = 1;
    static int64_t t_last = 0;
    int now = gpio_get_level(BTN_PIN);
    int64_t t = esp_timer_get_time();
    if (now == 0 && last == 1 && t - t_last > 200000) {
        tb = (tb + 1) % NTB;
        t_last = t;
    }
    last = now;
}

static void roll_step(uint32_t dt)
{
    static int64_t last = 0;
    int64_t now = esp_timer_get_time();
    if (now - last < dt) { vTaskDelay(1); return; }
    last = now;

    memmove(roll_buf, roll_buf + 1, (N - 1) * sizeof(uint16_t));
    roll_buf[N - 1] = adc_read();

    char txt[32];
    int n = snprintf(txt, sizeof(txt), "ROLL ");
    n += fmt_tb(txt + n, sizeof(txt) - n, dt);
    int mv = to_mv(roll_buf[N - 1]);
    snprintf(txt + n, sizeof(txt) - n, "%d.%02dV", mv / 1000, (mv % 1000) / 10);

    fb_clear();
    draw_grid();
    draw_trace(roll_buf);
    draw_text(0, txt);
    oled_flush();
}

static void triggered_step(uint32_t dt)
{
    int64_t t = esp_timer_get_time();
    for (int i = 0; i < 2 * N; i++) {
        cap[i] = adc_read();
        t += dt;
        while (esp_timer_get_time() < t) { }
    }

    uint16_t mn = 4095, mx = 0;
    for (int i = 0; i < 2 * N; i++) {
        if (cap[i] < mn) mn = cap[i];
        if (cap[i] > mx) mx = cap[i];
    }
    int mid = (mn + mx) / 2;
    int hyst = (mx - mn) / 10;

    int start = 0, second = -1, found = 0;
    bool armed = false;
    if (mx - mn > 60) {
        for (int i = 0; i < 2 * N; i++) {
            if (!armed && cap[i] < mid - hyst) {
                armed = true;
            } else if (armed && cap[i] >= mid) {
                if (found == 0) {
                    if (i >= N) break;
                    start = i;
                } else {
                    second = i;
                    break;
                }
                found++;
                armed = false;
            }
        }
    }
    int freq = 0;
    if (second > start)
        freq = (int)(1e6 / ((double)(second - start) * dt) + 0.5);

    char txt[32];
    int n = fmt_tb(txt, sizeof(txt), dt);
    int vpp = to_mv(mx) - to_mv(mn);
    n += snprintf(txt + n, sizeof(txt) - n, "%d.%02dVpp ", vpp / 1000, (vpp % 1000) / 10);
    if (freq > 0) snprintf(txt + n, sizeof(txt) - n, "%dHz", freq);

    fb_clear();
    draw_grid();
    draw_trace(cap + start);
    draw_text(0, txt);
    oled_flush();

    vTaskDelay(1);
}

static void i2c_setup(void)
{
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = SDA_PIN,
        .scl_io_num = SCL_PIN,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    i2c_master_bus_handle_t bus;
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &bus));

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = OLED_ADDR,
        .scl_speed_hz = 400000,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus, &dev_cfg, &oled));
}

static void adc_setup(void)
{
    adc_oneshot_unit_init_cfg_t ucfg = { .unit_id = ADC_UNIT_1 };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&ucfg, &adc));

    adc_oneshot_chan_cfg_t ccfg = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc, ADC_CH, &ccfg));

    adc_cali_curve_fitting_config_t cc = {
        .unit_id = ADC_UNIT_1,
        .chan = ADC_CH,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };
    cali_ok = (adc_cali_create_scheme_curve_fitting(&cc, &cali) == ESP_OK);
    ESP_LOGI(TAG, "Calibración ADC: %s", cali_ok ? "OK" : "no disponible");
}

static void button_setup(void)
{
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << BTN_PIN,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io));
}

void app_main(void)
{
    i2c_setup();
    oled_init();
    adc_setup();
    button_setup();

    while (1) {
        check_button();
        uint32_t dt = TB_US[tb];
        if (dt >= 30000) roll_step(dt);
        else             triggered_step(dt);
    }
}
