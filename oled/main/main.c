/*
 * ======================================================================================
 *                                  PACKAGE GUARD PRO (FINAL V2)
 * ======================================================================================
 */

 #include <stdio.h>
 #include <string.h>
 #include <math.h>
 #include <ctype.h>
 #include <time.h>
 #include <sys/time.h>
 #include <sys/unistd.h>
 #include <sys/stat.h>
 
 #include "freertos/FreeRTOS.h"
 #include "freertos/task.h"
 #include "freertos/semphr.h"
 #include "esp_log.h"
 #include "nvs_flash.h"
 #include "nvs.h"
 #include "esp_wifi.h"
 #include "esp_event.h"
 #include "esp_http_server.h"
 #include "esp_mac.h"
 #include "esp_random.h"
 #include "esp_sntp.h"
 
 // --- SD CARD & SPI ---
 #include "esp_vfs_fat.h"
 #include "sdmmc_cmd.h"
 #include "driver/spi_master.h"
 
 #include "mqtt_client.h"
 #include "cJSON.h"
 
 #include "driver/gpio.h"
 #include "driver/ledc.h"
 #include "esp_adc/adc_oneshot.h"
 
 // --- RGB LED ---
 #include "led_strip.h"
 
 #include "nimble/nimble_port.h"
 #include "nimble/nimble_port_freertos.h"
 #include "host/ble_hs.h"
 #include "host/util/util.h"
 #include "services/gap/ble_svc_gap.h"
 #include "services/gatt/ble_svc_gatt.h"
 
 #include <i2cdev.h>
 #include <tsl2591.h>
 #include <mpu6050.h>
 #include <bmp280.h>
 
 #include "oledDriver.h"
 #include "page.h"
 
 static const char *TAG = "GUARD_PRO";
 
 // --- PINY ---
 #define I2C_SDA 8
 #define I2C_SCL 9
 #define I2C_PORT I2C_NUM_0
 
 #define PIN_BUZZER 17
 #define PIN_BTN 4
 #define PIN_MOTOR 5
 #define PIN_BATTERY 6 
 #define PIN_MPU_INT 1
 
 // --- PIN RGB (Dla ESP32-S3 DevKit najczęściej 48 lub 38) ---
 #define PIN_RGB 48 
 
 // --- PINY SD CARD (SPI) ---
 #define SD_CS   10
 #define SD_MOSI 11
 #define SD_CLK  12
 #define SD_MISO 13
 #define SPI_HOST_ID SPI2_HOST 
 
 #define ADDR_MPU 0x68
 #define ADDR_BME 0x76
 
 // --- STRUKTURY ---
 typedef struct {
     float lux, temp, hum, pres;
     float acc_x, acc_y, acc_z, acc_mag;
     float battery_voltage;
     bool is_armed;
     int32_t alarm_count;
     bool wifi_connected;
     bool mqtt_connected;
     bool ble_connected;
     bool sd_mounted;      
     bool display_active;
 } SystemState;
 
 SystemState sys = { .is_armed = true, .alarm_count = 0, .display_active = false, .sd_mounted = false };
 
 // --- ZMIENNE GLOBALNE ---
 char conf_ssid[32], conf_pass[64], conf_user[32], conf_mqtt[128];
 char dev_mac[13], topic_data[128], topic_cmd[128], topic_event[128];
 char setup_pin[9];
 
 volatile int view_mode_timer = 0;
 uint8_t ble_addr_type;
 bool is_setup_mode = false;
 
 esp_mqtt_client_handle_t mqtt_client = NULL;
 httpd_handle_t server_handle = NULL;
 static TaskHandle_t sensor_task_handle = NULL;
 SemaphoreHandle_t i2c_mutex = NULL;
 SemaphoreHandle_t sd_mutex = NULL; 
 adc_oneshot_unit_handle_t adc_handle = NULL;
 sdmmc_card_t *sd_card = NULL;
 
 // --- ZMIENNA GLOBALNA DLA LED ---
 led_strip_handle_t led_strip; 
 
 void ble_app_advertise(void);
 
 // --- RGB LED UTILS ---
 void setup_rgb_led() {
     led_strip_config_t strip_config = {
         .strip_gpio_num = PIN_RGB,
         .max_leds = 1,
     };
     led_strip_rmt_config_t rmt_config = {
         .resolution_hz = 10 * 1000 * 1000, // 10MHz
     };
     ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip));
     led_strip_clear(led_strip);
 }
 
 void set_rgb_color(uint8_t r, uint8_t g, uint8_t b) {
     if (led_strip) {
         led_strip_set_pixel(led_strip, 0, r, g, b);
         led_strip_refresh(led_strip);
     }
 }
 
 // --- UTILS ---
 void setup_sntp() {
     esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
     esp_sntp_setservername(0, "pool.ntp.org");
     esp_sntp_init();
     setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
     tzset();
 }
 
 void get_time_str(char *buffer, size_t size) {
     time_t now; struct tm timeinfo; time(&now); localtime_r(&now, &timeinfo);
     strftime(buffer, size, "%H:%M:%S", &timeinfo);
 }
 
 void get_datetime_str(char *buffer, size_t size) {
     time_t now; struct tm timeinfo; time(&now); localtime_r(&now, &timeinfo);
     strftime(buffer, size, "%Y-%m-%d %H:%M:%S", &timeinfo);
 }
 
 // --- SD CARD ---
 void setup_sd_card() {
     esp_err_t ret;
     sd_mutex = xSemaphoreCreateMutex();
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
         // Sprawdź czy plik istnieje, jak nie to stwórz nagłówek
         FILE* f = fopen("/sdcard/log.csv", "r");
         if (f == NULL) {
             f = fopen("/sdcard/log.csv", "w");
             if (f) { 
                 // ZMIANA: ROZSZERZONY NAGŁÓWEK CSV
                 fprintf(f, "Timestamp,Type,Val,Acc,Temp,Lux,Pres,Hum,Bat,Armed,Alarms\n"); 
                 fclose(f); 
             }
         } else { fclose(f); }
     }
 }
 
 void sd_log_event(const char* type, float val) {
     if (!sys.sd_mounted) return;
     if (xSemaphoreTake(sd_mutex, pdMS_TO_TICKS(500))) {
         FILE* f = fopen("/sdcard/log.csv", "a");
         if (f != NULL) {
             char time_buf[32]; get_datetime_str(time_buf, sizeof(time_buf));
             // ZMIANA: ZAPISUJEMY WSZYSTKIE DANE
             fprintf(f, "%s,%s,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%d,%ld\n", 
                 time_buf, 
                 type, val, 
                 sys.acc_mag, sys.temp, sys.lux, sys.pres, sys.hum, sys.battery_voltage, 
                 sys.is_armed ? 1 : 0, sys.alarm_count
             );
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
 
 // --- BLE ---
 static int device_info_cb(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg) {
     char info[64]; sprintf(info, "ARMED:%d ALARMS:%ld BAT:%.1fV", sys.is_armed, sys.alarm_count, sys.battery_voltage);
     int rc = os_mbuf_append(ctxt->om, info, strlen(info)); return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
 }
 
 static const struct ble_gatt_svc_def gatt_svcs[] = {
     { .type = BLE_GATT_SVC_TYPE_PRIMARY, .uuid = BLE_UUID128_DECLARE(0xFB, 0x34, 0x9B, 0x5F, 0x80, 0x00, 0x00, 0x80, 0x00, 0x10, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x00),
       .characteristics = (struct ble_gatt_chr_def[]) { { .uuid = BLE_UUID128_DECLARE(0xFB, 0x34, 0x9B, 0x5F, 0x80, 0x00, 0x00, 0x80, 0x00, 0x10, 0x00, 0x00, 0x01, 0xFF, 0x00, 0x00), .flags = BLE_GATT_CHR_F_READ, .access_cb = device_info_cb }, { 0 } }, }, { 0 },
 };
 
 void ble_app_advertise(void) {
     ble_gap_adv_stop();
     struct ble_gap_adv_params adv_params; struct ble_hs_adv_fields fields;
     memset(&fields, 0, sizeof fields); fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
     static char name_buf[32];
     if (is_setup_mode) sprintf(name_buf, "GUARD_SETUP");
     else if (sys.is_armed) sprintf(name_buf, "G:ON A:%ld", sys.alarm_count);
     else sprintf(name_buf, "G:OFF B:%.1f", sys.battery_voltage);
     fields.name = (uint8_t *)name_buf; fields.name_len = strlen(name_buf); fields.name_is_complete = 1;
     ble_gap_adv_set_fields(&fields);
     ble_hs_id_infer_auto(0, &ble_addr_type);
     memset(&adv_params, 0, sizeof adv_params); adv_params.conn_mode = BLE_GAP_CONN_MODE_UND; adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
     ble_gap_adv_start(ble_addr_type, NULL, BLE_HS_FOREVER, &adv_params, NULL, NULL);
 }
 
 void ble_app_on_sync(void) { ble_hs_util_ensure_addr(0); ble_hs_id_infer_auto(0, &ble_addr_type); }
 void ble_host_task(void *param) { nimble_port_run(); nimble_port_freertos_deinit(); }
 void setup_ble_nimble() { nimble_port_init(); ble_svc_gap_init(); ble_svc_gatt_init(); ble_gatts_count_cfg(gatt_svcs); ble_gatts_add_svcs(gatt_svcs); ble_hs_cfg.sync_cb = ble_app_on_sync; nimble_port_freertos_init(ble_host_task); }
 
 // --- HW ---
 void setup_buzzer_pwm() {
     ledc_timer_config_t t = { .speed_mode=LEDC_LOW_SPEED_MODE, .timer_num=LEDC_TIMER_0, .duty_resolution=LEDC_TIMER_10_BIT, .freq_hz=2000, .clk_cfg=LEDC_AUTO_CLK }; ledc_timer_config(&t);
     ledc_channel_config_t c = { .speed_mode=LEDC_LOW_SPEED_MODE, .channel=LEDC_CHANNEL_0, .timer_sel=LEDC_TIMER_0, .intr_type=LEDC_INTR_DISABLE, .gpio_num=PIN_BUZZER, .duty=0, .hpoint=0 }; ledc_channel_config(&c);
 }
 void buzzer_beep(bool on) { ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, on ? 512 : 0); ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0); }
 void setup_adc() { adc_oneshot_unit_init_cfg_t i = { .unit_id = ADC_UNIT_1 }; adc_oneshot_new_unit(&i, &adc_handle); adc_oneshot_chan_cfg_t c = { .bitwidth = ADC_BITWIDTH_DEFAULT, .atten = ADC_ATTEN_DB_12 }; adc_oneshot_config_channel(adc_handle, ADC_CHANNEL_5, &c); }
 void read_battery() { int r; if(adc_oneshot_read(adc_handle, ADC_CHANNEL_5, &r)==ESP_OK) sys.battery_voltage=(r*3.3f/4095.0f)*2.0f; }
 
 // --- MQTT ---
 void url_decode(char *src, char *dst) { char a, b; while (*src) { if ((*src == '%') && ((a = src[1]) && (b = src[2])) && (isxdigit((int)a) && isxdigit((int)b))) { if (a >= 'a') a -= 'a' - 'A'; if (a >= 'A') a -= ('A' - 10); else a -= '0'; if (b >= 'a') b -= 'a' - 'A'; if (b >= 'A') b -= ('A' - 10); else b -= '0'; *dst++ = 16 * a + b; src += 3; } else if (*src == '+') { *dst++ = ' '; src++; } else { *dst++ = *src++; } } *dst = '\0'; }
 esp_err_t load_wifi_conf() { nvs_handle_t h; if(nvs_open("cfg", NVS_READONLY, &h)!=ESP_OK) return ESP_FAIL; size_t l=32; nvs_get_str(h,"ssid",conf_ssid,&l); l=64; nvs_get_str(h,"pass",conf_pass,&l); l=32; nvs_get_str(h,"user",conf_user,&l); l=128; nvs_get_str(h,"mqtt",conf_mqtt,&l); nvs_close(h); return ESP_OK; }
 
 void publish_data() {
    if (sys.sd_mounted) sd_log_event("STATUS_UPDATE", sys.temp);
    if (!sys.mqtt_connected) return;

    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "temp", sys.temp);
    cJSON_AddNumberToObject(root, "hum", sys.hum);
    cJSON_AddNumberToObject(root, "pres", sys.pres);
    cJSON_AddNumberToObject(root, "bat", sys.battery_voltage);
    
    // WYSYŁAMY WSZYSTKIE OSIE
    cJSON_AddNumberToObject(root, "ax", sys.acc_x);
    cJSON_AddNumberToObject(root, "ay", sys.acc_y);
    cJSON_AddNumberToObject(root, "az", sys.acc_z);
    cJSON_AddNumberToObject(root, "g", sys.acc_mag); // Wypadkowa

    cJSON_AddBoolToObject(root, "armed", sys.is_armed);
    cJSON_AddNumberToObject(root, "alarms", sys.alarm_count);
    
    time_t now; time(&now);
    cJSON_AddNumberToObject(root, "ts", (long)now);

    char *out = cJSON_PrintUnformatted(root);
    esp_mqtt_client_publish(mqtt_client, topic_data, out, 0, 1, 0);
    free(out);
    cJSON_Delete(root);
}

 void publish_event(const char* type, float val) {
     sd_log_event(type, val);
     if (!sys.mqtt_connected) return;
     cJSON *root = cJSON_CreateObject(); cJSON_AddStringToObject(root, "type", type); cJSON_AddNumberToObject(root, "val", val);
     time_t now; time(&now); cJSON_AddNumberToObject(root, "ts", (long)now);
     char *out = cJSON_PrintUnformatted(root); esp_mqtt_client_publish(mqtt_client, topic_event, out, 0, 1, 0); free(out); cJSON_Delete(root);
 }
 
 static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
     esp_mqtt_event_handle_t event = event_data;
     if (event_id == MQTT_EVENT_CONNECTED) { sys.mqtt_connected = true; esp_mqtt_client_subscribe(mqtt_client, topic_cmd, 1); }
     else if (event_id == MQTT_EVENT_DATA) {
         cJSON *json = cJSON_ParseWithLength(event->data, event->data_len);
         if (json) {
             cJSON *item = cJSON_GetObjectItem(json, "set");
             if (cJSON_IsString(item)) {
                 bool changed = false;
                 if (strcmp(item->valuestring, "ARM") == 0) { sys.is_armed = true; changed = true; sd_log_event("CMD_ARMED", 1); }
                 else if (strcmp(item->valuestring, "DISARM") == 0) { sys.is_armed = false; changed = true; sd_log_event("CMD_DISARMED", 0); }
                 if(changed) { save_persistent_state(); if(view_mode_timer > 0) ble_app_advertise(); }
             }
             cJSON_Delete(json);
         }
     }
 }
 
 static void IRAM_ATTR mpu_isr_handler(void *arg) {
    if (sensor_task_handle != NULL) {
        BaseType_t x = pdFALSE; 
        vTaskNotifyGiveFromISR(sensor_task_handle, &x); 
        if(x) portYIELD_FROM_ISR();
    }
}
void setup_mpu_wom(mpu6050_dev_t *dev) {
    if(xSemaphoreTake(i2c_mutex, portMAX_DELAY)) {
        i2c_dev_t *base_dev = (i2c_dev_t *)dev;
        uint8_t val;

        // 1. Reset i wybudzenie
        val = 0x00; i2c_dev_write_reg(base_dev, 0x6B, &val, 1);
        vTaskDelay(pdMS_TO_TICKS(10));

        // 2. Konfiguracja Akcelerometru i High Pass Filter (HPF)
        // Musimy włączyć HPF (High Pass Filter), żeby sensor ignorował stałą grawitację 1g,
        // a reagował tylko na ZMIANĘ (ruch).
        // 0x01 w rejestrze 0x1C ustawia pasmo 5Hz dla HPF.
        val = 0x01; i2c_dev_write_reg(base_dev, 0x1C, &val, 1); 

        // 3. Próg ruchu (Motion Threshold)
        // Wartość 12 = ok. 0.38g. Bardzo responsywne dla paczki.
        val = 12;   i2c_dev_write_reg(base_dev, 0x1F, &val, 1); 

        // 4. Czas trwania (Motion Duration)
        // 1ms (minimalny czas trwania ruchu, żeby wyzwolić alarm)
        val = 1;    i2c_dev_write_reg(base_dev, 0x20, &val, 1); 

        // 5. Włączenie przerwania Motion Detection (Bit 6)
        val = 0x40; i2c_dev_write_reg(base_dev, 0x38, &val, 1); 

        xSemaphoreGive(i2c_mutex);
    }
}
 
 // --- SENSOR TASK ---
 void sensor_task(void *arg) {
    sensor_task_handle = xTaskGetCurrentTaskHandle();
    mpu6050_dev_t mpu = {0}; mpu6050_init_desc(&mpu, ADDR_MPU, I2C_PORT, I2C_SDA, I2C_SCL); mpu6050_init(&mpu); setup_mpu_wom(&mpu);
    bmp280_t bme = {0}; bmp280_params_t bp; bmp280_init_default_params(&bp); bmp280_init_desc(&bme, ADDR_BME, I2C_PORT, I2C_SDA, I2C_SCL); bmp280_init(&bme, &bp);

    mpu6050_acceleration_t acc;
    uint8_t mpu_int_status = 0;
    TickType_t last_env_read = 0;

    while (1) {
        // Czekamy na przerwanie (ruch) - timeout 100ms dla ciągłości odczytów
        uint32_t notified = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(100));
        
        if(xSemaphoreTake(i2c_mutex, portMAX_DELAY)) {
            // Kasujemy flagę przerwania w MPU
            i2c_dev_read_reg((i2c_dev_t*)&mpu, 0x3A, &mpu_int_status, 1);
            
            // Pobieramy pełne dane
            mpu6050_get_acceleration(&mpu, &acc);
            
            // Zapisujemy oasie do struktury globalnej (dla MQTT)
            sys.acc_x = acc.x;
            sys.acc_y = acc.y;
            sys.acc_z = acc.z;
            sys.acc_mag = sqrtf(acc.x*acc.x + acc.y*acc.y + acc.z*acc.z);
            
            xSemaphoreGive(i2c_mutex);

            // LOGIKA ALARMU WSTRZĄSOWEGO
            if (sys.is_armed) {
                float diff = fabsf(1.0f - sys.acc_mag);
                
                // Warunek: Przerwanie sprzętowe + weryfikacja programowa (>0.35g odchyłki)
                if (((notified > 0) && (mpu_int_status & 0x40)) || diff > 0.35f) {
                     sys.alarm_count++; 
                     save_persistent_state();
                     publish_event("ALARM_SHOCK", sys.acc_mag);
                     
                     ESP_LOGW(TAG, "!!! ALARM WSTRZASOWY (G: %.2f) !!!", sys.acc_mag);
                     
                     buzzer_beep(true); gpio_set_level(PIN_MOTOR, 1); set_rgb_color(255, 0, 0);
                     vTaskDelay(pdMS_TO_TICKS(600));
                     buzzer_beep(false); gpio_set_level(PIN_MOTOR, 0); led_strip_clear(led_strip);
                     
                     vTaskDelay(pdMS_TO_TICKS(500)); // Czas na uspokojenie czujnika
                }
            }
        }

        // Odczyt BME i wysyłka statusu co 2 sekundy
        if (xTaskGetTickCount() - last_env_read > pdMS_TO_TICKS(2000)) {
            last_env_read = xTaskGetTickCount();
            if(xSemaphoreTake(i2c_mutex, portMAX_DELAY)) {
                bmp280_read_float(&bme, &sys.temp, &sys.pres, &sys.hum);
                xSemaphoreGive(i2c_mutex);
            }
            read_battery();
            publish_data();
        }
    }
}
 
 // --- EKRAN ---
 void display_task(void *arg) {
     if(xSemaphoreTake(i2c_mutex, portMAX_DELAY)) { screen_init(I2C_SDA, I2C_SCL); xSemaphoreGive(i2c_mutex); }
     char buf[32], time_buf[10]; bool blink = false;
 
     int led_blink_timer = 0;
 
     while(1) {
         if (is_setup_mode) {
             led_blink_timer++;
             if (led_blink_timer > 6) { 
                 set_rgb_color(0, 0, 50);
                 vTaskDelay(pdMS_TO_TICKS(50));
                 led_strip_clear(led_strip);
                 led_blink_timer = 0;
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
                     // ZMIANA: Zamiast Baterii (B:) jest Temperatura (T:)
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
                 sys.display_active = false; ble_gap_adv_stop(); 
             }
         }
         vTaskDelay(pdMS_TO_TICKS(500));
     }
 }
 
 // --- SERVER HTTP & WIFI ---
 esp_err_t root_get_handler(httpd_req_t *req) { return httpd_resp_send(req, html_page, HTTPD_RESP_USE_STRLEN); }
 esp_err_t save_post_handler(httpd_req_t *req) {
     char b[256]; int r = httpd_req_recv(req, b, req->content_len); if (r <= 0) return ESP_FAIL; b[r] = 0;
     char sr[32]={0}, pr[64]={0}, ur[32]={0}, mr[128]={0}, s[32], p[64], u[32], m[128];
     char *ptr = strstr(b, "ssid="); if(ptr) sscanf(ptr+5, "%[^&]", sr); ptr = strstr(b, "pass="); if(ptr) sscanf(ptr+5, "%[^&]", pr);
     ptr = strstr(b, "uid=");  if(ptr) sscanf(ptr+4, "%[^&]", ur); ptr = strstr(b, "mqtt="); if(ptr) sscanf(ptr+5, "%[^&]", mr);
     url_decode(sr, s); url_decode(pr, p); url_decode(ur, u); url_decode(mr, m);
     nvs_handle_t h; nvs_open("cfg", NVS_READWRITE, &h); nvs_set_str(h, "ssid", s); nvs_set_str(h, "pass", p); nvs_set_str(h, "user", u); nvs_set_str(h, "mqtt", m); nvs_commit(h); nvs_close(h);
     httpd_resp_send(req, "Saved. Rebooting...", -1); vTaskDelay(pdMS_TO_TICKS(1500)); esp_restart(); return ESP_OK;
 }
 static void wifi_handler(void* arg, esp_event_base_t b, int32_t id, void* d) { if (id == WIFI_EVENT_STA_START) esp_wifi_connect(); else if (id == WIFI_EVENT_STA_DISCONNECTED) { sys.wifi_connected = false; esp_wifi_connect(); } else if (id == IP_EVENT_STA_GOT_IP) { sys.wifi_connected = true; setup_sntp(); } }
 
 void app_main(void) {
     i2c_mutex = xSemaphoreCreateMutex();
     esp_err_t ret = nvs_flash_init(); if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) { nvs_flash_erase(); nvs_flash_init(); }
     esp_netif_init(); esp_event_loop_create_default(); i2cdev_init(); setup_ble_nimble();
     setup_sd_card(); setup_rgb_led(); 
 
     gpio_config_t io_conf = { .pin_bit_mask = (1ULL << PIN_MPU_INT), .mode = GPIO_MODE_INPUT, .pull_up_en = GPIO_PULLUP_ENABLE, .intr_type = GPIO_INTR_NEGEDGE }; gpio_config(&io_conf);
     setup_buzzer_pwm(); setup_adc(); gpio_set_direction(PIN_MOTOR, GPIO_MODE_OUTPUT);
     gpio_config_t btn_conf = { .pin_bit_mask = (1ULL << PIN_BTN), .mode = GPIO_MODE_INPUT, .pull_up_en = GPIO_PULLUP_ENABLE, .pull_down_en = GPIO_PULLDOWN_DISABLE, .intr_type = GPIO_INTR_DISABLE }; gpio_config(&btn_conf);
 
     load_persistent_state(); sd_log_event("SYSTEM_BOOT", 1.0);
 
     if (load_wifi_conf() == ESP_OK && strlen(conf_ssid) > 0) {
         is_setup_mode = false;
         uint8_t mac[6]; esp_read_mac(mac, ESP_MAC_WIFI_STA); sprintf(dev_mac, "%02X%02X%02X%02X%02X%02X", mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
         sprintf(topic_data,  "packageguard/user1/%s/data", dev_mac); sprintf(topic_event, "packageguard/user1/%s/event", dev_mac); sprintf(topic_cmd,   "packageguard/user1/%s/cmd",   dev_mac);
         esp_netif_create_default_wifi_sta(); wifi_init_config_t w_cfg = WIFI_INIT_CONFIG_DEFAULT(); esp_wifi_init(&w_cfg);
         esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_handler, NULL, NULL); esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_handler, NULL, NULL);
         wifi_config_t sta_cfg = {0}; strcpy((char*)sta_cfg.sta.ssid, conf_ssid); strcpy((char*)sta_cfg.sta.password, conf_pass);
         esp_wifi_set_mode(WIFI_MODE_STA); esp_wifi_set_config(WIFI_IF_STA, &sta_cfg); esp_wifi_start();
         esp_mqtt_client_config_t m_cfg = { .broker.address.uri = conf_mqtt }; mqtt_client = esp_mqtt_client_init(&m_cfg);
         esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL); esp_mqtt_client_start(mqtt_client);
         xTaskCreate(sensor_task, "Sens", 4096, NULL, 10, &sensor_task_handle); xTaskCreate(display_task, "Disp", 4096, NULL, 5, NULL);
     } else {
         is_setup_mode = true; view_mode_timer = 9999;
         uint32_t r = esp_random() % 90000000 + 10000000; sprintf(setup_pin, "%lu", (unsigned long)r);
         esp_netif_create_default_wifi_ap(); wifi_init_config_t a_cfg = WIFI_INIT_CONFIG_DEFAULT(); esp_wifi_init(&a_cfg);
         wifi_config_t ap_cfg = { .ap = { .ssid = "PackageGuard_Setup", .max_connection = 1, .authmode = WIFI_AUTH_WPA2_PSK } }; strcpy((char*)ap_cfg.ap.password, setup_pin);
         esp_wifi_set_mode(WIFI_MODE_AP); esp_wifi_set_config(WIFI_IF_AP, &ap_cfg); esp_wifi_start();
         httpd_config_t h_cfg = HTTPD_DEFAULT_CONFIG(); if (httpd_start(&server_handle, &h_cfg) == ESP_OK) { httpd_register_uri_handler(server_handle, &(httpd_uri_t){.uri="/", .method=HTTP_GET, .handler=root_get_handler}); httpd_register_uri_handler(server_handle, &(httpd_uri_t){.uri="/save", .method=HTTP_POST, .handler=save_post_handler}); }
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