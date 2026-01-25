#include <stdio.h>
#include <string.h>
#include "esp_event.h"
#include "config.h"
#include "hal.h"
#include "network.h"
#include "logic.h"
#include "esp_mac.h"
#include "esp_random.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "i2cdev.h"
#include "oledDriver.h"

// --- ZMIENNE GLOBALNE (DEFINICJE) ---
SystemState sys = { .is_armed = true, .alarm_count = 0, .display_active = false, .sd_mounted = false };
DeviceConfig current_config;
SemaphoreHandle_t i2c_mutex = NULL;
SemaphoreHandle_t sd_mutex = NULL;
char conf_ssid[32], conf_pass[64], conf_user[32], conf_mqtt[128];
char dev_mac[13], topic_data[128], topic_cmd[128], topic_event[128];
char setup_pin[9];
volatile int view_mode_timer = 0;
bool is_setup_mode = false;

static TaskHandle_t sensor_task_handle = NULL;

static void IRAM_ATTR mpu_isr_handler(void *arg) {
    if (sensor_task_handle != NULL) {
        BaseType_t x = pdFALSE; vTaskNotifyGiveFromISR(sensor_task_handle, &x); if(x) portYIELD_FROM_ISR();
    }
}

void app_main(void) {
    i2c_mutex = xSemaphoreCreateMutex();
    sd_mutex = xSemaphoreCreateMutex();

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) { nvs_flash_erase(); nvs_flash_init(); }
    
    // ŁADUJEMY KONFIGURACJĘ PRZED WSZYSTKIM INNYM
    load_device_config();

    esp_netif_init(); 
    esp_event_loop_create_default();
    i2cdev_init();

    setup_ble_nimble();
    setup_hal_init(); // SD, RGB, Buzzer, ADC

    load_persistent_state(); 
    sd_log_event("SYSTEM_BOOT", 1.0);

    // GPIO Setup
    gpio_config_t io_conf = { .pin_bit_mask = (1ULL << PIN_MPU_INT), .mode = GPIO_MODE_INPUT, .pull_up_en = GPIO_PULLUP_ENABLE, .intr_type = GPIO_INTR_NEGEDGE }; gpio_config(&io_conf);
    gpio_set_direction(PIN_MOTOR, GPIO_MODE_OUTPUT);
    gpio_config_t btn_conf = { .pin_bit_mask = (1ULL << PIN_BTN), .mode = GPIO_MODE_INPUT, .pull_up_en = GPIO_PULLUP_ENABLE, .pull_down_en = GPIO_PULLDOWN_DISABLE, .intr_type = GPIO_INTR_DISABLE }; gpio_config(&btn_conf);

    if (load_wifi_conf() == ESP_OK && strlen(conf_ssid) > 0) {
        is_setup_mode = false;
        uint8_t mac[6]; esp_read_mac(mac, ESP_MAC_WIFI_STA); sprintf(dev_mac, "%02X%02X%02X%02X%02X%02X", mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
        sprintf(topic_data,  "packageguard/user1/%s/data", dev_mac); sprintf(topic_event, "packageguard/user1/%s/event", dev_mac); sprintf(topic_cmd,   "packageguard/user1/%s/cmd",   dev_mac);
        setup_network_sta();
        xTaskCreate(sensor_task, "Sens", 4096, NULL, 10, &sensor_task_handle);
        logic_set_sensor_task_handle(sensor_task_handle);
        xTaskCreate(display_task, "Disp", 4096, NULL, 5, NULL);
    } else {
        is_setup_mode = true; view_mode_timer = 9999;
        uint32_t r = esp_random() % 90000000 + 10000000; sprintf(setup_pin, "%lu", (unsigned long)r);
        setup_network_ap();
        xTaskCreate(display_task, "Disp", 4096, NULL, 5, NULL);
    }

    gpio_install_isr_service(0);
    gpio_isr_handler_add(PIN_MPU_INT, mpu_isr_handler, NULL);

    while (1) {
        if (gpio_get_level(PIN_BTN) == 0) {
            vTaskDelay(pdMS_TO_TICKS(50)); if (gpio_get_level(PIN_BTN) != 0) continue; 
            int hold_ms = 0; bool reset_triggered = false;
            while (gpio_get_level(PIN_BTN) == 0) {
                vTaskDelay(pdMS_TO_TICKS(100)); hold_ms += 100;
                if (hold_ms % 1000 == 0) { buzzer_beep(true); vTaskDelay(pdMS_TO_TICKS(50)); buzzer_beep(false); }
                if (hold_ms >= 3000) { buzzer_beep(true); if(xSemaphoreTake(i2c_mutex, pdMS_TO_TICKS(100))) { screen_clear_buffer(); screen_write_text(0, 20, "RESETOWANIE..."); screen_refresh(); xSemaphoreGive(i2c_mutex); } vTaskDelay(pdMS_TO_TICKS(1000)); buzzer_beep(false); nvs_flash_erase(); esp_restart(); reset_triggered = true; break; }
            }
            if (!reset_triggered) { view_mode_timer = 60; ble_app_advertise(); }
        }
        if (view_mode_timer > 0) { view_mode_timer--; vTaskDelay(pdMS_TO_TICKS(1000)); } else vTaskDelay(pdMS_TO_TICKS(100));
    }
}