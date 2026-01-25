#pragma once
#include "config.h"
#include "esp_err.h"

esp_err_t load_wifi_conf(void);
void setup_network_sta(void);
void setup_network_ap(void);
void publish_data(void);
void publish_event(const char* type, float val);
void setup_ble_nimble(void);
void ble_app_advertise(void);