#include "oledDriver.h"
#include <string.h>
#include "font.h"
// UŻYWAMY BIBLIOTEKI I2CDEV (Wspólna szyna dla czujników i ekranu)
#include <i2cdev.h> 

static uint8_t frame_buffer[1024];
static const uint8_t scroll_speed_map[] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07};

// Deskryptor urządzenia (zarządzany przez i2cdev)
static i2c_dev_t oled_dev = { 0 };

static void send_cmd(uint8_t cmd) {
    uint8_t data[2] = {0x00, cmd};
    // Biblioteka sama zadba o Mutexy i dostęp do portu
    i2c_dev_write(&oled_dev, NULL, 0, data, 2);
}

void screen_init(int sda_pin, int scl_pin) {
    // 1. Zerujemy strukturę
    memset(&oled_dev, 0, sizeof(i2c_dev_t));

    // 2. Konfiguracja struktury i2cdev
    oled_dev.port = I2C_NUM_0;
    oled_dev.addr = SCREEN_I2C_ADDR;
    oled_dev.cfg.sda_io_num = sda_pin;
    oled_dev.cfg.scl_io_num = scl_pin;
    oled_dev.cfg.master.clk_speed = 400000; // 400kHz
    oled_dev.cfg.sda_pullup_en = GPIO_PULLUP_ENABLE;
    oled_dev.cfg.scl_pullup_en = GPIO_PULLUP_ENABLE;

    // 3. Tworzymy Mutex (To jest kluczowe dla i2cdev!)
    // Ta funkcja nie instaluje sterownika sprzętowego, tylko przygotowuje soft
    i2c_dev_create_mutex(&oled_dev);

    // Sekwencja startowa OLED
    uint8_t init_sequence[] = {
        0xAE, 0x20, 0x00, 0xB0, 0xC8, 0x00, 0x10, 0x40,
        0x81, 0xFF, 0xA1, 0xA6, 0xA8, 0x3F, 0xA4, 0xD3,
        0x00, 0xD5, 0xF0, 0xD9, 0x22, 0xDA, 0x12, 0xDB,
        0x20, 0x8D, 0x14, 0xAF
    };
    
    for (int i = 0; i < sizeof(init_sequence); i++) {
        send_cmd(init_sequence[i]);
    }
}

void screen_refresh() {
    for (int p = 0; p < 8; p++) {
        send_cmd(0xB0 + p);
        send_cmd(0x00);
        send_cmd(0x10);
        
        uint8_t i2c_data[129];
        i2c_data[0] = 0x40;
        memcpy(&i2c_data[1], &frame_buffer[p * 128], 128);
        
        i2c_dev_write(&oled_dev, NULL, 0, i2c_data, 129);
    }
}

// --- Reszta funkcji bez zmian (logika bufora) ---
void screen_clear_buffer() { memset(frame_buffer, 0, sizeof(frame_buffer)); }
void screen_put_pixel(int x, int y, int active) {
    if (x >= 128 || y >= 64 || x < 0 || y < 0) return;
    int index = x + (y / 8) * 128;
    int bit = y % 8;
    if (active) frame_buffer[index] |= (1 << bit);
    else        frame_buffer[index] &= ~(1 << bit);
}
static const uint8_t* get_font_char(char c) {
    if (c < 32 || c > 127) c = 32;
    return font5x7[c - 32];
}
void screen_write_text(int x, int y, const char *text) {
    while (*text) {
        const uint8_t *bitmap = get_font_char(*text);
        for (int i = 0; i < 5; i++) {
            for (int j = 0; j < 8; j++) {
                if (bitmap[i] & (1 << j)) screen_put_pixel(x + i, y + j, 1);
            }
        }
        x += 6; text++;
    }
}
void screen_write_text_clipped(int x, int y, const char *text, int min_x, int max_x) {
    while (*text) {
        const uint8_t *bitmap = get_font_char(*text);
        for (int i = 0; i < 5; i++) {
            int draw_x = x + i;
            if (draw_x >= min_x && draw_x <= max_x) {
                for (int j = 0; j < 8; j++) {
                    if (bitmap[i] & (1 << j)) screen_put_pixel(draw_x, y + j, 1);
                }
            }
        }
        x += 6; text++;
    }
}
void screen_draw_bitmap(int x, int y, int w, int h, const bool *bitmap) {
    for (int row = 0; row < h; row++) {
        for (int col = 0; col < w; col++) {
            bool val = bitmap[row * w + col];
            screen_put_pixel(x + col, y + row, val ? 1 : 0);
        }
    }
}
void screen_set_contrast(uint8_t contrast) { send_cmd(0x81); send_cmd(contrast); }
void screen_display_on(bool on) { send_cmd(on ? 0xAF : 0xAE); }
void screen_invert(bool invert) { send_cmd(invert ? 0xA7 : 0xA6); }
void screen_flip(bool rotated) { if (rotated) { send_cmd(0xA0); send_cmd(0xC0); } else { send_cmd(0xA1); send_cmd(0xC8); } }
void screen_scroll_stop(void) { send_cmd(0x2E); }
void screen_scroll_right(uint8_t start_page, uint8_t end_page, uint8_t speed) {
    screen_scroll_stop(); if(speed > 7) speed = 7;
    send_cmd(0x26); send_cmd(0x00); send_cmd(start_page & 0x07); 
    send_cmd(scroll_speed_map[speed]); send_cmd(end_page & 0x07); 
    send_cmd(0x00); send_cmd(0xFF); send_cmd(0x2F);
}
void screen_scroll_left(uint8_t start_page, uint8_t end_page, uint8_t speed) {
    screen_scroll_stop(); if(speed > 7) speed = 7;
    send_cmd(0x27); send_cmd(0x00); send_cmd(start_page & 0x07); 
    send_cmd(scroll_speed_map[speed]); send_cmd(end_page & 0x07); 
    send_cmd(0x00); send_cmd(0xFF); send_cmd(0x2F);
}