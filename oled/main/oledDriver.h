#pragma once

#include <stdint.h>
#include <stdbool.h>
#define SCREEN_I2C_ADDR 0x3C

/**
 * @param sda_pin GPIO SDA pin.
 * @param scl_pin GPIO SCL pin.
 */
void screen_init(int sda_pin, int scl_pin);

void screen_clear_buffer(void);
void screen_refresh(void);
void screen_put_pixel(int x, int y, int active);
void screen_write_text(int x, int y, const char *text);
void screen_write_text_clipped(int x, int y, const char *text, int min_x, int max_x);
void screen_draw_bitmap(int x, int y, int w, int h, const bool *bitmap);

void screen_set_contrast(uint8_t contrast);
void screen_display_on(bool on);
void screen_invert(bool invert);
void screen_flip(bool rotated);
void screen_scroll_right(uint8_t start_page, uint8_t end_page, uint8_t speed);
void screen_scroll_left(uint8_t start_page, uint8_t end_page, uint8_t speed);
void screen_scroll_stop(void);