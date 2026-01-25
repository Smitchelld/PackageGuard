#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"

// --- PINY ---
#define I2C_SDA 8
#define I2C_SCL 9
#define I2C_PORT I2C_NUM_0

#define PIN_BUZZER 17
#define PIN_BTN 4
#define PIN_MOTOR 5
#define PIN_BATTERY 6 
#define PIN_MPU_INT 1
#define PIN_RGB 48 

// --- SD CARD ---
#define SD_CS   10
#define SD_MOSI 11
#define SD_CLK  12
#define SD_MISO 13
#define SPI_HOST_ID SPI2_HOST 

// --- ADRESY I2C ---
#define ADDR_MPU 0x68
#define ADDR_BME 0x76

// --- Tryby Pracy ---
typedef enum {
    MODE_PERFORMANCE,
    MODE_BALANCED
} OperatingMode;

// --- Stany BLE ---
typedef enum {
    BLE_STATE_SLEEP,
    BLE_STATE_PAIRING,
    BLE_STATE_CONNECTED
} BLEState;

// --- STRUKTURA KONFIGURACJI ---
typedef struct {
    // Progi alarmowe
    float shock_threshold_g;
    float temp_min_c;
    float temp_max_c;
    float hum_max_percent;

    // Przełączniki alarmów
    bool shock_alarm_enabled;
    bool temp_alarm_enabled;
    bool hum_alarm_enabled;
    
    // Ustawienia operacyjne
    uint32_t status_interval_sec;
    OperatingMode op_mode;
    bool stealth_mode_enabled;

} DeviceConfig;

// --- STRUKTURA STANU ---
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

// --- ZMIENNE GLOBALNE (EXTERN) ---
extern SystemState sys;
extern SemaphoreHandle_t i2c_mutex;
extern SemaphoreHandle_t sd_mutex;
extern char dev_mac[13];
extern bool is_setup_mode;
extern char setup_pin[9];
extern volatile int view_mode_timer;
extern DeviceConfig current_config;

// Konfiguracja WiFi/MQTT
extern char conf_ssid[32], conf_pass[64], conf_user[32], conf_mqtt[128];
extern char topic_data[128], topic_cmd[128], topic_event[128];

// --- Zmienne do zarządzania trybami ---
extern OperatingMode current_mode;
extern BLEState current_ble_state;
extern volatile int ble_pairing_timer;