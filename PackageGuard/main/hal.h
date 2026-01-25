#pragma once
#include "config.h"

void setup_hal_init(void);
void setup_sd_card(void);
void sd_log_event(const char* type, float val);
void save_persistent_state(void);
void load_persistent_state(void);
void setup_buzzer_pwm(void);
void buzzer_beep(bool on);
void setup_rgb_led(void);
void set_rgb_color(uint8_t r, uint8_t g, uint8_t b);
void led_strip_clear_wrapper(void);
void read_battery(void);
void get_datetime_str(char *buffer, size_t size);
void load_device_config(void);
void save_device_config(void);