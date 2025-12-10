#include "epd_board.h"
#include "epd_display.h"
#include <epdiy.h>

#include <driver/gpio.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <stdbool.h>
#include <stddef.h>

// Minimal local copy of the LCD bus / config types used by epdiy's lcd_driver.
typedef struct {
    gpio_num_t data[16];
    gpio_num_t clock;
    gpio_num_t ckv;
    gpio_num_t start_pulse;
    gpio_num_t leh;
    gpio_num_t stv;
} lcd_bus_config_t;

typedef struct {
    size_t pixel_clock;
    int ckv_high_time;
    int line_front_porch;
    int le_high_time;
    int bus_width;
    lcd_bus_config_t bus;
} LcdEpdConfig_t;

// Prototypes from epdiy's lcd_driver.c
void epd_lcd_init(const LcdEpdConfig_t* config, int display_width, int display_height);
void epd_lcd_deinit(void);

static const char* TAG = "epdiy_paper_s3_board";

// Pin mapping for M5Stack Paper S3, derived from M5GFX Bus_EPD config.
// Data bus (8-bit)
#define PAPER_S3_D0 GPIO_NUM_6
#define PAPER_S3_D1 GPIO_NUM_14
#define PAPER_S3_D2 GPIO_NUM_7
#define PAPER_S3_D3 GPIO_NUM_12
#define PAPER_S3_D4 GPIO_NUM_9
#define PAPER_S3_D5 GPIO_NUM_11
#define PAPER_S3_D6 GPIO_NUM_8
#define PAPER_S3_D7 GPIO_NUM_10

// Control lines
#define PAPER_S3_PIN_PWR GPIO_NUM_46
#define PAPER_S3_PIN_SPH GPIO_NUM_13  // horizontal start pulse
#define PAPER_S3_PIN_SPV GPIO_NUM_17  // vertical start pulse
#define PAPER_S3_PIN_OE  GPIO_NUM_45  // output enable
#define PAPER_S3_PIN_LE  GPIO_NUM_15  // latch enable
#define PAPER_S3_PIN_CL  GPIO_NUM_16  // source driver clock
#define PAPER_S3_PIN_CKV GPIO_NUM_18  // gate driver clock

static bool s_powered = false;

static void paper_s3_config_pins(void) {
    gpio_config_t io = { 0 };
    io.intr_type = GPIO_INTR_DISABLE;
    io.mode = GPIO_MODE_OUTPUT;
    io.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io.pull_up_en = GPIO_PULLUP_DISABLE;

    // Power / control pins
    io.pin_bit_mask = (1ULL << PAPER_S3_PIN_PWR) | (1ULL << PAPER_S3_PIN_SPH)
                    | (1ULL << PAPER_S3_PIN_SPV) | (1ULL << PAPER_S3_PIN_OE)
                    | (1ULL << PAPER_S3_PIN_LE) | (1ULL << PAPER_S3_PIN_CL)
                    | (1ULL << PAPER_S3_PIN_CKV);
    gpio_config(&io);

    // Default all low (safe / powered off)
    gpio_set_level(PAPER_S3_PIN_PWR, 0);
    gpio_set_level(PAPER_S3_PIN_OE, 0);
    gpio_set_level(PAPER_S3_PIN_SPV, 0);
    gpio_set_level(PAPER_S3_PIN_SPH, 0);
    gpio_set_level(PAPER_S3_PIN_LE, 0);
    gpio_set_level(PAPER_S3_PIN_CL, 0);
    gpio_set_level(PAPER_S3_PIN_CKV, 0);
}

static void paper_s3_bus_init(void) {
    const EpdDisplay_t* display = epd_get_display();

    lcd_bus_config_t bus = {
        .data = {
            PAPER_S3_D0,
            PAPER_S3_D1,
            PAPER_S3_D2,
            PAPER_S3_D3,
            PAPER_S3_D4,
            PAPER_S3_D5,
            PAPER_S3_D6,
            PAPER_S3_D7,
            -1, -1, -1, -1, -1, -1, -1, -1,
        },
        .clock = PAPER_S3_PIN_CL,
        .ckv = PAPER_S3_PIN_CKV,
        .start_pulse = PAPER_S3_PIN_SPH,
        .leh = PAPER_S3_PIN_LE,
        .stv = PAPER_S3_PIN_SPV,
    };

    LcdEpdConfig_t cfg = {
        .pixel_clock = display->bus_speed * 1000 * 1000,
        .ckv_high_time = 60,
        .line_front_porch = 4,
        .le_high_time = 4,
        .bus_width = display->bus_width,
        .bus = bus,
    };

    ESP_LOGI(TAG, "Init PaperS3 EPD bus %dx%d, bus_width=%d", display->width, display->height, display->bus_width);
    epd_lcd_init(&cfg, display->width, display->height);
}

static void paper_s3_power_control(bool on) {
    if (on == s_powered) {
        return;
    }
    s_powered = on;

    if (on) {
        // Roughly mirror M5GFX Bus_EPD::powerControl(true)
        gpio_set_level(PAPER_S3_PIN_OE, 1);
        vTaskDelay(pdMS_TO_TICKS(1));
        gpio_set_level(PAPER_S3_PIN_PWR, 1);
        vTaskDelay(pdMS_TO_TICKS(1));
        gpio_set_level(PAPER_S3_PIN_SPV, 1);
        vTaskDelay(pdMS_TO_TICKS(1));
    } else {
        vTaskDelay(pdMS_TO_TICKS(1));
        gpio_set_level(PAPER_S3_PIN_PWR, 0);
        vTaskDelay(pdMS_TO_TICKS(1));
        gpio_set_level(PAPER_S3_PIN_OE, 0);
        vTaskDelay(pdMS_TO_TICKS(1));
        gpio_set_level(PAPER_S3_PIN_SPV, 0);
    }
}

static void epd_paper_s3_init(uint32_t epd_row_width) {
    (void)epd_row_width;
    paper_s3_config_pins();
    paper_s3_bus_init();
}

static void epd_paper_s3_deinit(void) {
    epd_lcd_deinit();
    paper_s3_power_control(false);
}

static void epd_paper_s3_set_ctrl(epd_ctrl_state_t* state, const epd_ctrl_state_t* const mask) {
    (void)mask;
    // Treat ep_output_enable as the main power / OE signal.
    paper_s3_power_control(state->ep_output_enable);
}

static void epd_paper_s3_poweron(epd_ctrl_state_t* state) {
    state->ep_output_enable = true;
    epd_ctrl_state_t mask = {
        .ep_output_enable = true,
    };
    epd_paper_s3_set_ctrl(state, &mask);
}

static void epd_paper_s3_poweroff(epd_ctrl_state_t* state) {
    state->ep_output_enable = false;
    epd_ctrl_state_t mask = {
        .ep_output_enable = true,
    };
    epd_paper_s3_set_ctrl(state, &mask);
}

static float epd_paper_s3_temperature(void) {
    return 20.0f;
}

static void epd_paper_s3_set_vcom(int value) {
    (void)value;
}

const EpdBoardDefinition paper_s3_board = {
    .init = epd_paper_s3_init,
    .deinit = epd_paper_s3_deinit,
    .set_ctrl = epd_paper_s3_set_ctrl,
    .poweron = epd_paper_s3_poweron,
    .measure_vcom = NULL,
    .poweroff = epd_paper_s3_poweroff,
    .set_vcom = epd_paper_s3_set_vcom,
    .get_temperature = epd_paper_s3_temperature,
    .gpio_set_direction = NULL,
    .gpio_read = NULL,
    .gpio_write = NULL,
};
