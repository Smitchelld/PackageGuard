#include "hal.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/ledc.h"
#include "esp_adc/adc_oneshot.h"
#include "led_strip.h"
#include <time.h>
#include <sys/time.h>

#define CONFIG_STORAGE_NAMESPACE "dev_config"
static const char *TAG = "HAL";

static adc_oneshot_unit_handle_t adc_handle = NULL;
static led_strip_handle_t led_strip;
static sdmmc_card_t *sd_card = NULL;

void get_datetime_str(char *buffer, size_t size) {
    time_t now; struct tm timeinfo; time(&now); localtime_r(&now, &timeinfo);
    strftime(buffer, size, "%Y-%m-%d %H:%M:%S", &timeinfo);
}

// --- SD CARD ---
void setup_sd_card() {
    esp_err_t ret;
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = true, .max_files = 2, .allocation_unit_size = 16 * 1024
    };
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SPI_HOST_ID;
    host.max_freq_khz = 5000;
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = SD_MOSI, .miso_io_num = SD_MISO, .sclk_io_num = SD_CLK,
        .quadwp_io_num = -1, .quadhd_io_num = -1, .max_transfer_sz = 4000,
    };
    ret = spi_bus_initialize(host.slot, &bus_cfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) return;

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = SD_CS; slot_config.host_id = host.slot;

    ret = esp_vfs_fat_sdspi_mount("/sdcard", &host, &slot_config, &mount_config, &sd_card);
    if (ret == ESP_OK) {
        sys.sd_mounted = true;
        FILE* f = fopen("/sdcard/log.csv", "r");
        if (f == NULL) {
            f = fopen("/sdcard/log.csv", "w");
            if (f) { fprintf(f, "Timestamp,Type,Val,Acc,Temp,Lux,Pres,Hum,Bat,Armed,Alarms\n"); fclose(f); }
        } else { fclose(f); }
        ESP_LOGI(TAG, "SD Mounted");
    }
}

void sd_log_event(const char* type, float val) {
    if (!sys.sd_mounted) return;
    if (xSemaphoreTake(sd_mutex, pdMS_TO_TICKS(500))) {
        FILE* f = fopen("/sdcard/log.csv", "a");
        if (f != NULL) {
            char time_buf[32]; get_datetime_str(time_buf, sizeof(time_buf));
            fprintf(f, "%s,%s,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%d,%ld\n", 
                time_buf, type, val, sys.acc_mag, sys.temp, sys.lux, sys.pres, sys.hum, sys.battery_voltage, sys.is_armed ? 1 : 0, sys.alarm_count);
            fclose(f);
        }
        xSemaphoreGive(sd_mutex);
    }
}

// --- NVS ---
void save_persistent_state() {
    nvs_handle_t h;
    if (nvs_open("storage", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u8(h, "armed", sys.is_armed ? 1 : 0);
        nvs_set_i32(h, "alarms", sys.alarm_count);
        nvs_commit(h); nvs_close(h);
    }
}
void load_persistent_state() {
    nvs_handle_t h;
    if (nvs_open("storage", NVS_READONLY, &h) == ESP_OK) {
        uint8_t a = 1; nvs_get_u8(h, "armed", &a); sys.is_armed = (a == 1);
        nvs_get_i32(h, "alarms", &sys.alarm_count); nvs_close(h);
    }
}

// --- RGB & BUZZER & BAT ---
void setup_rgb_led() {
    led_strip_config_t strip_config = { .strip_gpio_num = PIN_RGB, .max_leds = 1 };
    led_strip_rmt_config_t rmt_config = { .resolution_hz = 10 * 1000 * 1000 };
    led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip);
    led_strip_clear(led_strip);
}
void set_rgb_color(uint8_t r, uint8_t g, uint8_t b) { if(led_strip){ led_strip_set_pixel(led_strip, 0, r, g, b); led_strip_refresh(led_strip); } }
void led_strip_clear_wrapper(void) { if(led_strip) led_strip_clear(led_strip); }

void setup_buzzer_pwm() {
    ledc_timer_config_t t = { .speed_mode=LEDC_LOW_SPEED_MODE, .timer_num=LEDC_TIMER_0, .duty_resolution=LEDC_TIMER_10_BIT, .freq_hz=2000, .clk_cfg=LEDC_AUTO_CLK }; ledc_timer_config(&t);
    ledc_channel_config_t c = { .speed_mode=LEDC_LOW_SPEED_MODE, .channel=LEDC_CHANNEL_0, .timer_sel=LEDC_TIMER_0, .intr_type=LEDC_INTR_DISABLE, .gpio_num=PIN_BUZZER, .duty=0, .hpoint=0 }; ledc_channel_config(&c);
}
void buzzer_beep(bool on) { ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, on ? 512 : 0); ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0); }

void setup_hal_init() {
    setup_sd_card(); setup_rgb_led(); setup_buzzer_pwm();
    adc_oneshot_unit_init_cfg_t i = { .unit_id = ADC_UNIT_1 }; adc_oneshot_new_unit(&i, &adc_handle);
    adc_oneshot_chan_cfg_t c = { .bitwidth = ADC_BITWIDTH_DEFAULT, .atten = ADC_ATTEN_DB_12 }; adc_oneshot_config_channel(adc_handle, ADC_CHANNEL_5, &c);
}
void read_battery() { int r; if(adc_oneshot_read(adc_handle, ADC_CHANNEL_5, &r)==ESP_OK) sys.battery_voltage=(r*3.3f/4095.0f)*2.0f; }

void load_device_config(void) {
    nvs_handle_t h;
    esp_err_t err = nvs_open(CONFIG_STORAGE_NAMESPACE, NVS_READONLY, &h);
    if (err == ESP_OK) {
        // Jeśli jest zapisana konfiguracja, odczytaj ją
        size_t required_size = 0;
        // Najpierw pobierz rozmiar, żeby uniknąć błędów
        if (nvs_get_blob(h, "config_blob", NULL, &required_size) == ESP_OK) {
            if (required_size == sizeof(DeviceConfig)) {
                nvs_get_blob(h, "config_blob", &current_config, &required_size);
                ESP_LOGI(TAG, "Zaladowano konfiguracje z NVS.");
            } else {
                ESP_LOGW(TAG, "Rozmiar konfiguracji w NVS nie zgadza sie. Ladowanie domyslnych.");
                err = ESP_FAIL; // Wymuś załadowanie domyślnych
            }
        }
        nvs_close(h);
    } 
    
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "Ladowanie domyslnych ustawien...");
        current_config.shock_threshold_g = 0.0f;
        current_config.temp_min_c = 0.0f;
        current_config.temp_max_c = 0.0f;
        current_config.hum_max_percent = 0.0f;
        current_config.pres_min_hpa = 0.0f;
        current_config.pres_max_hpa = 0.0f;
        current_config.bat_min_v = 0.0f;
        current_config.lux_min = 0.0f;
        current_config.lux_max = 0.0f;
    
        current_config.shock_alarm_enabled = false;
        current_config.temp_alarm_enabled = false;
        current_config.hum_alarm_enabled = false;
        current_config.pres_alarm_enabled = false;
        current_config.bat_alarm_enabled = false;
        current_config.light_alarm_enabled = false;
        
        current_config.action_buzzer_enabled = false;
        current_config.action_motor_enabled = false;
        current_config.action_led_enabled = false;
        current_config.stealth_mode_enabled = false;
    
        current_config.status_interval_sec = 10;
        current_config.op_mode = MODE_BALANCED;
        save_device_config();
    }
}

void save_device_config(void) {
    nvs_handle_t h;
    esp_err_t err = nvs_open(CONFIG_STORAGE_NAMESPACE, NVS_READWRITE, &h);
    if (err == ESP_OK) {
        err = nvs_set_blob(h, "config_blob", &current_config, sizeof(DeviceConfig));
        if (err == ESP_OK) {
            err = nvs_commit(h);
        }
        nvs_close(h);
        ESP_LOGI(TAG, "Zapisano konfiguracje w NVS. Status: %s", esp_err_to_name(err));
    }
}

void sd_save_offline(const char* topic, const char* json_payload) {
    if (!sys.sd_mounted) return;
    
    if (xSemaphoreTake(sd_mutex, pdMS_TO_TICKS(1000))) {
        FILE* f = fopen("/sdcard/sync.txt", "a"); 
        if (f != NULL) {
            fprintf(f, "%s|%s\n", topic, json_payload);
            fclose(f);
            ESP_LOGI("SD_OFFLINE", "Zapisano offline.");
        } else {
            ESP_LOGE("SD_OFFLINE", "Blad otwarcia");
        }
        xSemaphoreGive(sd_mutex);
    }
}