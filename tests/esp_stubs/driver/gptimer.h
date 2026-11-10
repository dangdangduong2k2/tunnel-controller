#pragma once
#include "Arduino.h"
struct FakeTimer;
typedef FakeTimer *gptimer_handle_t;
struct gptimer_config_t {
    int clk_src, direction;
    uint32_t resolution_hz;
};
struct gptimer_alarm_event_data_t {
    uint64_t alarm_value, count_value;
};
struct gptimer_alarm_config_t {
    uint64_t alarm_count;
};
struct gptimer_event_callbacks_t {
    bool (*on_alarm)(gptimer_handle_t, const gptimer_alarm_event_data_t *, void *);
};
struct FakeTimer {
    uint64_t count = 0, alarm = 0;
    bool armed = false;
    gptimer_event_callbacks_t callbacks = {};
    void *context = nullptr;
};
#define GPTIMER_CLK_SRC_DEFAULT 0
#define GPTIMER_COUNT_UP 0
esp_err_t gptimer_new_timer(const gptimer_config_t *, gptimer_handle_t *);
esp_err_t gptimer_register_event_callbacks(gptimer_handle_t, const gptimer_event_callbacks_t *,
                                           void *);
esp_err_t gptimer_enable(gptimer_handle_t);
esp_err_t gptimer_start(gptimer_handle_t);
esp_err_t gptimer_set_alarm_action(gptimer_handle_t, const gptimer_alarm_config_t *);
esp_err_t gptimer_get_raw_count(gptimer_handle_t, uint64_t *);
