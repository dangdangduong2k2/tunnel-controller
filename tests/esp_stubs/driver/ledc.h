#pragma once
#include "Arduino.h"
typedef int ledc_timer_bit_t;
struct ledc_timer_config_t {
    int speed_mode, timer_num, duty_resolution;
    uint32_t freq_hz;
    int clk_cfg;
};
struct ledc_channel_config_t {
    int gpio_num, speed_mode, channel, timer_sel;
    uint32_t duty;
};
#define LEDC_LOW_SPEED_MODE 0
#define LEDC_CHANNEL_2 2
#define LEDC_TIMER_1 1
#define LEDC_TIMER_8_BIT 8
#define LEDC_USE_XTAL_CLK 0
esp_err_t ledc_stop(int, int, int);
esp_err_t ledc_timer_config(const ledc_timer_config_t *);
esp_err_t ledc_channel_config(const ledc_channel_config_t *);
esp_err_t ledc_set_duty(int, int, uint32_t);
esp_err_t ledc_update_duty(int, int);
