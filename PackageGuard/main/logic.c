#include "logic.h"
#include "config.h"
#include "hal.h"
#include "network.h"
#include "i2cdev.h"
#include "mpu6050.h"
#include "bmp280.h"
#include "oledDriver.h"
#include <math.h>
#include <string.h>
#include <time.h>

static const char *TAG = "LOGIC";
static TaskHandle_t sensor_task_handle = NULL;

void logic_set_sensor_task_handle(TaskHandle_t h) { sensor_task_handle = h; }

// ISR Handler musi być tutaj lub w main, ale sensor_task jest tutaj
// Ponieważ ISR to funkcja statyczna, musimy ją wyeksponować lub przenieść logikę ISR do main.
// Dla porządku: sensor_task jest tutaj.

void setup_mpu_wom(mpu6050_dev_t *dev) {
    if(xSemaphoreTake(i2c_mutex, portMAX_DELAY)) {
        i2c_dev_t *base_dev = (i2c_dev_t *)dev; uint8_t val;
        val=0x00; i2c_dev_write_reg(base_dev, 0x6B, &val, 1); vTaskDelay(pdMS_TO_TICKS(10));
        val=0x01; i2c_dev_write_reg(base_dev, 0x1C, &val, 1); val=12; i2c_dev_write_reg(base_dev, 0x1F, &val, 1); // 0.4g
        val=1; i2c_dev_write_reg(base_dev, 0x20, &val, 1); val=0x40; i2c_dev_write_reg(base_dev, 0x38, &val, 1);
        xSemaphoreGive(i2c_mutex);
    }
}

void sensor_task(void *arg) {
    mpu6050_dev_t mpu = {0}; mpu6050_init_desc(&mpu, ADDR_MPU, I2C_PORT, I2C_SDA, I2C_SCL); mpu6050_init(&mpu); setup_mpu_wom(&mpu);
    bmp280_t bme = {0}; bmp280_params_t bp; bmp280_init_default_params(&bp); bmp280_init_desc(&bme, ADDR_BME, I2C_PORT, I2C_SDA, I2C_SCL); bmp280_init(&bme, &bp);

    mpu6050_acceleration_t acc;
    uint8_t mpu_int_status = 0;
    TickType_t last_status_publish = 0;
    
    // Zmienne do unikania spamu alarmami
    TickType_t last_temp_alarm_time = 0;
    TickType_t last_hum_alarm_time = 0;
    const TickType_t alarm_debounce_ms = pdMS_TO_TICKS(60000); // Co najmniej minuta między alarmami tego samego typu

    while (1) {
        uint32_t notified = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000)); // Pętla działa co 1s
        
        // Odczyt sensorów
        if(xSemaphoreTake(i2c_mutex, portMAX_DELAY)) {
            i2c_dev_read_reg((i2c_dev_t*)&mpu, 0x3A, &mpu_int_status, 1);
            mpu6050_get_acceleration(&mpu, &acc);
            sys.acc_x = acc.x; sys.acc_y = acc.y; sys.acc_z = acc.z;
            sys.acc_mag = sqrtf(acc.x*acc.x + acc.y*acc.y + acc.z*acc.z);
            
            bmp280_read_float(&bme, &sys.temp, &sys.pres, &sys.hum);
            xSemaphoreGive(i2c_mutex);
        }
        read_battery();

        // LOGIKA ALARMÓW
        if (sys.is_armed) {
            // 1. Alarm Wstrząsowy
            if (current_config.shock_alarm_enabled) {
                float diff = fabsf(1.0f - sys.acc_mag);
                if (((notified > 0) && (mpu_int_status & 0x40)) || diff > current_config.shock_threshold_g) {
                     sys.alarm_count++; 
                     save_persistent_state();
                     publish_event("ALARM_SHOCK", sys.acc_mag);
                     if (!current_config.stealth_mode_enabled) {
                        buzzer_beep(true); gpio_set_level(PIN_MOTOR, 1); set_rgb_color(255, 0, 0);
                        vTaskDelay(pdMS_TO_TICKS(600));
                        buzzer_beep(false); gpio_set_level(PIN_MOTOR, 0); led_strip_clear_wrapper();
                     }
                     vTaskDelay(pdMS_TO_TICKS(1000)); // Chwila spokoju po alarmie
                }
            }
            
            // 2. Alarm Temperatury
            if (current_config.temp_alarm_enabled) {
                if ((sys.temp > current_config.temp_max_c || sys.temp < current_config.temp_min_c) &&
                    (xTaskGetTickCount() - last_temp_alarm_time > alarm_debounce_ms))
                {
                    last_temp_alarm_time = xTaskGetTickCount();
                    sys.alarm_count++;
                    save_persistent_state();
                    publish_event("ALARM_TEMP", sys.temp);
                    if (!current_config.stealth_mode_enabled) {
                        for(int i=0; i<3; i++) { buzzer_beep(true); vTaskDelay(pdMS_TO_TICKS(100)); buzzer_beep(false); vTaskDelay(pdMS_TO_TICKS(100)); }
                    }
                }
            }

            // 3. Alarm Wilgotności
            if (current_config.hum_alarm_enabled) {
                if (sys.hum > current_config.hum_max_percent && 
                    (xTaskGetTickCount() - last_hum_alarm_time > alarm_debounce_ms))
                {
                    last_hum_alarm_time = xTaskGetTickCount();
                    sys.alarm_count++;
                    save_persistent_state();
                    publish_event("ALARM_HUMIDITY", sys.hum);
                    if (!current_config.stealth_mode_enabled) {
                         for(int i=0; i<3; i++) { buzzer_beep(true); vTaskDelay(pdMS_TO_TICKS(100)); buzzer_beep(false); vTaskDelay(pdMS_TO_TICKS(100)); }
                    }
                }
            }
        }

        // LOGIKA WYSYŁANIA STATUSU
        if (xTaskGetTickCount() - last_status_publish > pdMS_TO_TICKS(current_config.status_interval_sec * 1000)) {
            last_status_publish = xTaskGetTickCount();
            publish_data();
        }
    }
}

void get_time_str(char *buffer, size_t size) {
    time_t now; struct tm timeinfo; time(&now); localtime_r(&now, &timeinfo);
    strftime(buffer, size, "%H:%M:%S", &timeinfo);
}

void display_task(void *arg) {
    if(xSemaphoreTake(i2c_mutex, portMAX_DELAY)) { screen_init(I2C_SDA, I2C_SCL); xSemaphoreGive(i2c_mutex); }
    char buf[32], time_buf[10]; bool blink = false;
    int led_blink_timer = 0;

    while(1) {
        if (is_setup_mode) {
            led_blink_timer++;
            if (led_blink_timer > 6) { 
                set_rgb_color(0, 0, 50); vTaskDelay(pdMS_TO_TICKS(50));
                led_strip_clear_wrapper(); led_blink_timer = 0;
            }
        }
        if (view_mode_timer > 0) {
            sys.display_active = true; blink = !blink;
            if(xSemaphoreTake(i2c_mutex, pdMS_TO_TICKS(200))) {
                screen_clear_buffer();
                if (is_setup_mode) {
                    screen_write_text(0,0, "!! SETUP MODE !!");
                    screen_write_text(0,16, "Wifi: PackageGuard");
                    char pin_info[20]; sprintf(pin_info, "PASS: %s", setup_pin); screen_write_text(0,32, pin_info);
                    screen_write_text(0,48, "IP: 192.168.4.1");
                } else {
                    get_time_str(time_buf, sizeof(time_buf));
                    char title[16]; sprintf(title, "%s %c", time_buf, blink ? '.' : ' ');
                    screen_write_text(0,0, "GUARD PRO"); screen_write_text(75,0, title);
                    sprintf(buf, "G:%.2f P:%.0f", sys.acc_mag, sys.pres); screen_write_text(0,18, buf);
                    sprintf(buf, "T:%.1fC H:%.0f%%", sys.temp, sys.hum); screen_write_text(0,34, buf);
                    screen_write_text(0, 52, sys.is_armed ? "[ARMED]" : "[DISARMED]");
                    if (sys.mqtt_connected) screen_write_text(108,18,"M");
                    if (sys.ble_connected) screen_write_text(108,34, "B");
                    if (sys.sd_mounted) screen_write_text(108, 52, "SD");
                }
                screen_refresh(); xSemaphoreGive(i2c_mutex);
            }
        } else {
            if (sys.display_active) {
                if(xSemaphoreTake(i2c_mutex, pdMS_TO_TICKS(200))) { screen_clear_buffer(); screen_refresh(); xSemaphoreGive(i2c_mutex); }
                sys.display_active = false; ble_app_advertise(); 
            }
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}