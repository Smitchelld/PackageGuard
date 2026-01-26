#include "network.h"
#include "hal.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "mqtt_client.h"
#include "cJSON.h"
#include "esp_sntp.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <ctype.h>
#include <string.h>

#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "page.h" 

static const char *TAG = "NET";
esp_mqtt_client_handle_t mqtt_client = NULL;
static httpd_handle_t server_handle = NULL;
uint8_t ble_addr_type;

// --- UUID SERWISU I CHARAKTERYSTYK (Zgodne z aplikacją Android) ---
// Service: c1356e62-fe2c-4861-a28a-446ada22f6d5
static const ble_uuid128_t gatt_svr_svc_uuid =
    BLE_UUID128_INIT(0xd5, 0xf6, 0x22, 0xda, 0x6a, 0x44, 0x8a, 0xa2, 0x61, 0x48, 0x2c, 0xfe, 0x62, 0x6e, 0x35, 0xc1);

// Status Char (READ): c1356e63-fe2c-4861-a28a-446ada22f6d5
static const ble_uuid128_t gatt_svr_chr_status_uuid =
    BLE_UUID128_INIT(0xd5, 0xf6, 0x22, 0xda, 0x6a, 0x44, 0x8a, 0xa2, 0x61, 0x48, 0x2c, 0xfe, 0x63, 0x6e, 0x35, 0xc1);

// Command Char (WRITE): c1356e64-fe2c-4861-a28a-446ada22f6d5
static const ble_uuid128_t gatt_svr_chr_cmd_uuid =
    BLE_UUID128_INIT(0xd5, 0xf6, 0x22, 0xda, 0x6a, 0x44, 0x8a, 0xa2, 0x61, 0x48, 0x2c, 0xfe, 0x64, 0x6e, 0x35, 0xc1);


// --- BLE CALLBACKS ---

static int ble_svc_gatt_handler_status(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg) {
    char buf[256];
    snprintf(buf, sizeof(buf), 
        "{\"t\":%.1f,\"h\":%.0f,\"p\":%.1f,\"bat\":%.2f,\"arm\":%d,\"al\":%ld}",
        sys.temp, sys.hum, sys.pres, sys.battery_voltage, sys.is_armed ? 1 : 0, sys.alarm_count);
    
    return os_mbuf_append(ctxt->om, buf, strlen(buf));
}

static int ble_svc_gatt_handler_command(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg) {
    char cmd[256];
    int len = ctxt->om->om_len;
    if (ble_hs_mbuf_to_flat(ctxt->om, cmd, sizeof(cmd)-1, NULL) != 0) return 1;
    cmd[len] = '\0';

    ESP_LOGI("BLE_CMD", "Odebrano: %s", cmd);

    // STEROWANIE PROSTE
    if (strcmp(cmd, "ARM") == 0) {
        sys.is_armed = true; 
        save_persistent_state();
        ESP_LOGI("BLE", "Uzbrojono przez BLE");
    } 
    else if (strcmp(cmd, "DISARM") == 0) {
        sys.is_armed = false; 
        save_persistent_state();
        ESP_LOGI("BLE", "Rozbrojono przez BLE");
    } 
    // PAROWANIE 
    else if (strncmp(cmd, "PAIR:", 5) == 0) {
        char* new_user = cmd + 5;
        nvs_handle_t h;
        if (nvs_open("cfg", NVS_READWRITE, &h) == ESP_OK) {
            nvs_set_str(h, "user", new_user);
            nvs_commit(h); nvs_close(h);
        }
        strncpy(conf_user, new_user, sizeof(conf_user) - 1);
        sprintf(topic_data,  "packageguard/%s/%s/data",  conf_user, dev_mac);
        sprintf(topic_event, "packageguard/%s/%s/event", conf_user, dev_mac);
        sprintf(topic_cmd,   "packageguard/%s/%s/cmd",   conf_user, dev_mac);
        if(mqtt_client) { esp_mqtt_client_stop(mqtt_client); esp_mqtt_client_start(mqtt_client); }
        ESP_LOGW("BLE", "Zmieniono właściciela na: %s", conf_user);
    } 
    else if (strncmp(cmd, "CFG:", 4) == 0) {
        cJSON *json = cJSON_Parse(cmd + 4);
        if (json) {
            cJSON *i;
            // Przełączniki alarmów
            if((i = cJSON_GetObjectItem(json, "s_en"))) current_config.shock_alarm_enabled = cJSON_IsTrue(i);
            if((i = cJSON_GetObjectItem(json, "t_en"))) current_config.temp_alarm_enabled = cJSON_IsTrue(i);
            if((i = cJSON_GetObjectItem(json, "h_en"))) current_config.hum_alarm_enabled = cJSON_IsTrue(i);
            if((i = cJSON_GetObjectItem(json, "p_en"))) current_config.pres_alarm_enabled = cJSON_IsTrue(i);
            if((i = cJSON_GetObjectItem(json, "l_en"))) current_config.light_alarm_enabled = cJSON_IsTrue(i);
            if((i = cJSON_GetObjectItem(json, "b_en"))) current_config.bat_alarm_enabled = cJSON_IsTrue(i);
            
            // Przełączniki akcji
            if((i = cJSON_GetObjectItem(json, "buz"))) current_config.action_buzzer_enabled = cJSON_IsTrue(i);
            if((i = cJSON_GetObjectItem(json, "mot"))) current_config.action_motor_enabled = cJSON_IsTrue(i);
            if((i = cJSON_GetObjectItem(json, "led"))) current_config.action_led_enabled = i->type == cJSON_True;
            if((i = cJSON_GetObjectItem(json, "stl"))) current_config.stealth_mode_enabled = cJSON_IsTrue(i);

            // Wartości liczbowe
            if((i = cJSON_GetObjectItem(json, "shk"))) current_config.shock_threshold_g = i->valuedouble;
            if((i = cJSON_GetObjectItem(json, "tmn"))) current_config.temp_min_c = i->valuedouble;
            if((i = cJSON_GetObjectItem(json, "tmx"))) current_config.temp_max_c = i->valuedouble;
            if((i = cJSON_GetObjectItem(json, "bmn"))) current_config.bat_min_v = i->valuedouble;
            if((i = cJSON_GetObjectItem(json, "int"))) current_config.status_interval_sec = i->valueint;

            save_device_config();
            ESP_LOGI("BLE", "Zapisano pełną konfigurację z Bluetooth");
            cJSON_Delete(json);
        }
}

    buzzer_beep(true); vTaskDelay(pdMS_TO_TICKS(50)); buzzer_beep(false);
    return 0;
}
// Definicja usług GATT
static const struct ble_gatt_svc_def gatt_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &gatt_svr_svc_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {   // Status (READ)
                .uuid = &gatt_svr_chr_status_uuid.u,
                .access_cb = ble_svc_gatt_handler_status,
                .flags = BLE_GATT_CHR_F_READ,
            },
            {   // Command (WRITE)
                .uuid = &gatt_svr_chr_cmd_uuid.u,
                .access_cb = ble_svc_gatt_handler_command,
                .flags = BLE_GATT_CHR_F_WRITE,
            },
            { 0 }
        },
    },
    { 0 },
};

// --- FUNKCJE REKLAMOWANIA (Advertising) ---

// Tryb Uśpienia (Domyślny) - Nie można się połączyć
void ble_enter_sleep_mode(void) {
    ble_gap_adv_stop();
    struct ble_hs_adv_fields fields; memset(&fields, 0, sizeof fields);
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    
    char name_buf[32];
    sprintf(name_buf, "Guard %s", sys.is_armed ? "[A]" : "[D]");
    fields.name = (uint8_t *)name_buf; fields.name_len = strlen(name_buf); fields.name_is_complete = 1;
    
    ble_gap_adv_set_fields(&fields);
    
    struct ble_gap_adv_params adv_params; memset(&adv_params, 0, sizeof adv_params);
    adv_params.conn_mode = BLE_GAP_CONN_MODE_NON; // Non-connectable
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    
    ble_gap_adv_start(ble_addr_type, NULL, BLE_HS_FOREVER, &adv_params, NULL, NULL);
    ESP_LOGI(TAG, "BLE Sleep Mode (Non-connectable)");
}

// Obsługa zdarzeń GAP (Połączenie/Rozłączenie)
static int ble_gap_event(struct ble_gap_event *event, void *arg) {
    switch (event->type) {
        case BLE_GAP_EVENT_CONNECT:
            ESP_LOGI(TAG, "BLE Connected");
            sys.ble_connected = true;
            break;
        case BLE_GAP_EVENT_DISCONNECT:
            ESP_LOGI(TAG, "BLE Disconnected");
            sys.ble_connected = false;
            ble_enter_sleep_mode(); // Powrót do uśpienia po rozłączeniu
            break;
    }
    return 0;
}

// Tryb Parowania (Po wciśnięciu guzika) - Można się połączyć
void ble_enter_pairing_mode(void) {
    ble_gap_adv_stop();
    struct ble_hs_adv_fields fields; memset(&fields, 0, sizeof fields);
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    
    // Nazwa dla aplikacji (powinna być unikalna w logice skanowania)
    const char *name = "PKG_PAIR";
    fields.name = (uint8_t *)name; fields.name_len = strlen(name); fields.name_is_complete = 1;
    
    // Dodajemy UUID serwisu do ogłoszenia, żeby telefon łatwo nas znalazł
    // fields.uuids128 = (ble_uuid128_t[]){ gatt_svr_svc_uuid };
    // fields.num_uuids128 = 1;
    // fields.uuids128_is_complete = 1;

    ble_gap_adv_set_fields(&fields);
    
    struct ble_gap_adv_params adv_params; memset(&adv_params, 0, sizeof adv_params);
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND; // Undirected Connectable
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    
    ble_gap_adv_start(ble_addr_type, NULL, BLE_HS_FOREVER, &adv_params, ble_gap_event, NULL);
    ESP_LOGI(TAG, "BLE Pairing Mode (Connectable!)");
}

void ble_app_on_sync(void) {
    ble_hs_util_ensure_addr(0);
    ble_hs_id_infer_auto(0, &ble_addr_type);
    ble_enter_sleep_mode(); // Startuj w trybie uśpienia
}

void ble_host_task(void *param) { nimble_port_run(); nimble_port_freertos_deinit(); }

void setup_ble_nimble() {
    nimble_port_init();
    ble_svc_gap_init();
    ble_svc_gatt_init();
    ble_gatts_count_cfg(gatt_svcs);
    ble_gatts_add_svcs(gatt_svcs);
    ble_hs_cfg.sync_cb = ble_app_on_sync;
    nimble_port_freertos_init(ble_host_task);
}


// --- WIFI & MQTT (Obsługa konfiguracji, STA, AP) ---

void url_decode(char *src, char *dst) { char a, b; while (*src) { if ((*src == '%') && ((a = src[1]) && (b = src[2])) && (isxdigit((int)a) && isxdigit((int)b))) { if (a >= 'a') a -= 'a' - 'A'; if (a >= 'A') a -= ('A' - 10); else a -= '0'; if (b >= 'a') b -= 'a' - 'A'; if (b >= 'A') b -= ('A' - 10); else b -= '0'; *dst++ = 16 * a + b; src += 3; } else if (*src == '+') { *dst++ = ' '; src++; } else { *dst++ = *src++; } } *dst = '\0'; }

esp_err_t load_wifi_conf() {
    nvs_handle_t h;
    esp_err_t err;
    if(nvs_open("cfg", NVS_READONLY, &h) != ESP_OK) {
        ESP_LOGW("NET", "NVS 'cfg' nie istnieje. Ustawiam domyślne.");
        strcpy(conf_mqtt, "mqtt://192.168.137.1");
        strcpy(conf_user, "unassigned");
        return ESP_FAIL;
    }
    
    size_t l;
    l = sizeof(conf_ssid);
    if (nvs_get_str(h, "ssid", conf_ssid, &l) != ESP_OK) strcpy(conf_ssid, "");
    l = sizeof(conf_pass);
    if (nvs_get_str(h, "pass", conf_pass, &l) != ESP_OK) strcpy(conf_pass, "");
    l = sizeof(conf_user);
    err = nvs_get_str(h, "user", conf_user, &l);
    if (err != ESP_OK || strlen(conf_user) == 0) {
        strcpy(conf_user, "unassigned");
        ESP_LOGI("NET", "User nie znaleziony lub pusty. Ustawiam: unassigned");
    }
    l = sizeof(conf_mqtt);
    if (nvs_get_str(h, "mqtt", conf_mqtt, &l) != ESP_OK) {
        strcpy(conf_mqtt, "mqtt://192.168.137.1");
    }

    nvs_close(h);
    
    ESP_LOGI("NET", "Wczytano: User=[%s], MQTT=[%s]", conf_user, conf_mqtt);
    return ESP_OK;
}

void setup_sntp() { esp_sntp_setoperatingmode(SNTP_OPMODE_POLL); esp_sntp_setservername(0, "pool.ntp.org"); esp_sntp_init(); setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1); tzset(); }

static void wifi_handler(void* arg, esp_event_base_t b, int32_t id, void* d) {
    if (id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (id == WIFI_EVENT_STA_DISCONNECTED) {
        sys.wifi_connected = false;
        
        ESP_LOGW("WIFI", "Rozłączono. Czekam 10 sekund przed ponowną próbą...");
        
        static esp_timer_handle_t reconnect_timer;
        if (reconnect_timer == NULL) {
            const esp_timer_create_args_t timer_args = {
                .callback = (void*)esp_wifi_connect,
                .name = "reconnect_timer"
            };
            esp_timer_create(&timer_args, &reconnect_timer);
        }
        
        esp_timer_stop(reconnect_timer);
        esp_timer_start_once(reconnect_timer, 5 * 1000000);
        
    } else if (id == IP_EVENT_STA_GOT_IP) {
        sys.wifi_connected = true;
        ESP_LOGI("WIFI", "Połączono! MQTT ruszy za chwilę.");
    }
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    esp_mqtt_event_handle_t event = event_data;
    
    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI("MQTT", "POŁĄCZONO z brokerem: %s", conf_mqtt);
            sys.mqtt_connected = true;
            
            esp_mqtt_client_subscribe(mqtt_client, topic_cmd, 1);
            ESP_LOGI("MQTT", "Subskrypcja tematu: %s", topic_cmd);
            vTaskDelay(pdMS_TO_TICKS(2000)); 
            sync_offline_data();
            break;

        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGW("MQTT", "ROZŁĄCZONO z brokerem");
            sys.mqtt_connected = false;
            break;

        case MQTT_EVENT_ERROR:
            ESP_LOGE("MQTT", "BŁĄD PROTOKOŁU / TRANSPORTU");
            // Wymuszamy status false, żeby publikowanie danych od razu szło na SD
            sys.mqtt_connected = false;
            if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
                ESP_LOGE("MQTT", "Błąd TCP: errno=%d", event->error_handle->esp_transport_sock_errno);
            }
            break;

        case MQTT_EVENT_DATA:
            ESP_LOGI("MQTT", "Odebrano dane z tematu: %.*s", event->topic_len, event->topic);
            
            // 1. Sprawdź czy urządzenie nie jest w stanie "unassigned"
            if (strcmp(conf_user, "unassigned") == 0) {
                ESP_LOGW("MQTT", "Odmowa konfiguracji: Urządzenie nieprzypisane!");
                return;
            }

            // 2. Parsowanie JSON
            char *data_buf = malloc(event->data_len + 1);
            if (!data_buf) return;
            memcpy(data_buf, event->data, event->data_len);
            data_buf[event->data_len] = '\0';

            cJSON *json = cJSON_Parse(data_buf);
            if (json) {
                // OBSŁUGA KOMENDY "SET" (ARM / DISARM)
                cJSON *set_item = cJSON_GetObjectItem(json, "set");
                if (cJSON_IsString(set_item)) {
                    if (strcmp(set_item->valuestring, "ARM") == 0) {
                        sys.is_armed = true;
                        ESP_LOGW("MQTT", "ZDALNE UZBROJENIE");
                    } else if (strcmp(set_item->valuestring, "DISARM") == 0) {
                        sys.is_armed = false;
                        ESP_LOGW("MQTT", "ZDALNE ROZBROJENIE");
                    }
                    save_persistent_state();
                }

                // OBSŁUGA PEŁNEJ KONFIGURACJI (PROGI)
                cJSON *cfg = cJSON_GetObjectItem(json, "config");
                if (cfg) {
                    cJSON *i;
                    // Alarmy Enabled
                    if((i = cJSON_GetObjectItem(cfg, "shock_alarm_enabled"))) current_config.shock_alarm_enabled = cJSON_IsTrue(i);
                    if((i = cJSON_GetObjectItem(cfg, "temp_alarm_enabled")))  current_config.temp_alarm_enabled = cJSON_IsTrue(i);
                    if((i = cJSON_GetObjectItem(cfg, "hum_alarm_enabled")))   current_config.hum_alarm_enabled = cJSON_IsTrue(i);
                    if((i = cJSON_GetObjectItem(cfg, "pres_alarm_enabled")))  current_config.pres_alarm_enabled = cJSON_IsTrue(i);
                    if((i = cJSON_GetObjectItem(cfg, "light_alarm_enabled"))) current_config.light_alarm_enabled = cJSON_IsTrue(i);
                    if((i = cJSON_GetObjectItem(cfg, "bat_alarm_enabled")))   current_config.bat_alarm_enabled = cJSON_IsTrue(i);
                    
                    // Reakcje
                    if((i = cJSON_GetObjectItem(cfg, "action_buzzer_enabled"))) current_config.action_buzzer_enabled = cJSON_IsTrue(i);
                    if((i = cJSON_GetObjectItem(cfg, "action_motor_enabled")))  current_config.action_motor_enabled = cJSON_IsTrue(i);
                    if((i = cJSON_GetObjectItem(cfg, "action_led_enabled")))    current_config.action_led_enabled = cJSON_IsTrue(i);
                    if((i = cJSON_GetObjectItem(cfg, "stealth_mode_enabled")))  current_config.stealth_mode_enabled = cJSON_IsTrue(i);

                    // Progi liczbowe
                    if((i = cJSON_GetObjectItem(cfg, "shock_threshold_g"))) current_config.shock_threshold_g = i->valuedouble;
                    if((i = cJSON_GetObjectItem(cfg, "temp_min_c")))       current_config.temp_min_c = i->valuedouble;
                    if((i = cJSON_GetObjectItem(cfg, "temp_max_c")))       current_config.temp_max_c = i->valuedouble;
                    if((i = cJSON_GetObjectItem(cfg, "hum_max_percent")))  current_config.hum_max_percent = i->valuedouble;
                    if((i = cJSON_GetObjectItem(cfg, "pres_min_hpa")))     current_config.pres_min_hpa = i->valuedouble;
                    if((i = cJSON_GetObjectItem(cfg, "pres_max_hpa")))     current_config.pres_max_hpa = i->valuedouble;
                    if((i = cJSON_GetObjectItem(cfg, "bat_min_v")))        current_config.bat_min_v = i->valuedouble;
                    if((i = cJSON_GetObjectItem(cfg, "status_interval_sec"))) current_config.status_interval_sec = i->valueint;
                    
                    save_device_config();
                    ESP_LOGI("MQTT", "Zaktualizowano i zapisano pełną konfigurację sensorów.");
                    
                    buzzer_beep(true); vTaskDelay(pdMS_TO_TICKS(100)); buzzer_beep(false);
                }
                cJSON_Delete(json);
            }
            free(data_buf);
            break;

        default:
            break;
    }
}

void setup_network_sta() {
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t w_cfg = WIFI_INIT_CONFIG_DEFAULT(); esp_wifi_init(&w_cfg);
    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_handler, NULL, NULL);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_handler, NULL, NULL);
    wifi_config_t sta_cfg = {0}; strcpy((char*)sta_cfg.sta.ssid, conf_ssid); strcpy((char*)sta_cfg.sta.password, conf_pass);
    esp_wifi_set_mode(WIFI_MODE_STA); esp_wifi_set_config(WIFI_IF_STA, &sta_cfg); esp_wifi_start();

    esp_mqtt_client_config_t m_cfg = { .broker.address.uri = conf_mqtt };
    mqtt_client = esp_mqtt_client_init(&m_cfg);
    esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(mqtt_client);
}

// --- HTTP SERVER ---
esp_err_t root_get_handler(httpd_req_t *req) { return httpd_resp_send(req, html_page, HTTPD_RESP_USE_STRLEN); }
esp_err_t save_post_handler(httpd_req_t *req) {
    char b[256]; 
    int r = httpd_req_recv(req, b, req->content_len); 
    if (r <= 0) return ESP_FAIL; 
    b[r] = 0;

    char s[32]={0}, p[64]={0}, u[32]={0}, m[128]={0};
    char sr[32]={0}, pr[64]={0}, ur[32]={0}, mr[128]={0};

    char *ptr;
    ptr = strstr(b, "ssid="); if(ptr) sscanf(ptr+5, "%[^&]", sr);
    ptr = strstr(b, "pass="); if(ptr) sscanf(ptr+5, "%[^&]", pr);
    ptr = strstr(b, "mqtt="); if(ptr) sscanf(ptr+5, "%[^&]", mr);
    
    ptr = strstr(b, "uid=");  if(ptr) sscanf(ptr+4, "%[^&]", ur);

    url_decode(sr, s); 
    url_decode(pr, p); 
    url_decode(mr, m);
    url_decode(ur, u);

    nvs_handle_t h; 
    nvs_open("cfg", NVS_READWRITE, &h);
    nvs_set_str(h, "ssid", s); 
    nvs_set_str(h, "pass", p); 
    nvs_set_str(h, "mqtt", m);

    if (strlen(u) > 0) {
        nvs_set_str(h, "user", u);
    } else {
        size_t dummy_len;
        if (nvs_get_str(h, "user", NULL, &dummy_len) != ESP_OK) {
            nvs_set_str(h, "user", "unassigned");
        }
    }

    nvs_commit(h); nvs_close(h);
    
    httpd_resp_send(req, "Saved. Rebooting...", -1); 
    vTaskDelay(pdMS_TO_TICKS(1500)); 
    esp_restart(); 
    return ESP_OK;
}

void setup_network_ap() {
    esp_netif_create_default_wifi_ap(); wifi_init_config_t a_cfg = WIFI_INIT_CONFIG_DEFAULT(); esp_wifi_init(&a_cfg);
    wifi_config_t ap_cfg = { .ap = { .ssid = "PackageGuard_Setup", .max_connection = 1, .authmode = WIFI_AUTH_WPA2_PSK } }; strcpy((char*)ap_cfg.ap.password, setup_pin);
    esp_wifi_set_mode(WIFI_MODE_AP); esp_wifi_set_config(WIFI_IF_AP, &ap_cfg); esp_wifi_start();
    httpd_config_t h_cfg = HTTPD_DEFAULT_CONFIG();
    if (httpd_start(&server_handle, &h_cfg) == ESP_OK) {
        httpd_register_uri_handler(server_handle, &(httpd_uri_t){.uri="/", .method=HTTP_GET, .handler=root_get_handler});
        httpd_register_uri_handler(server_handle, &(httpd_uri_t){.uri="/save", .method=HTTP_POST, .handler=save_post_handler});
    }
}

// --- DATA PUBLISH ---
void publish_data() {
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "temp", sys.temp);
    cJSON_AddNumberToObject(root, "hum", sys.hum);
    cJSON_AddNumberToObject(root, "pres", sys.pres * 100);
    cJSON_AddNumberToObject(root, "lux", sys.lux);
    cJSON_AddNumberToObject(root, "bat", sys.battery_voltage);
    cJSON_AddNumberToObject(root, "g", sys.acc_mag);
    cJSON_AddBoolToObject(root, "armed", sys.is_armed);
    cJSON_AddNumberToObject(root, "alarms", sys.alarm_count);
    
    time_t now; time(&now);
    cJSON_AddNumberToObject(root, "ts", (long)now);

    char *out = cJSON_PrintUnformatted(root);
    
    if (sys.mqtt_connected) {
        esp_mqtt_client_publish(mqtt_client, topic_data, out, 0, 1, 0);
        sync_offline_data(); 
    } else {
        sd_save_offline(topic_data, out);
    }

    free(out);
    cJSON_Delete(root);
}

void publish_event(const char* type, float val) {
    sd_log_event(type, val);

    cJSON *root = cJSON_CreateObject();
    if (!root) return;
    
    cJSON_AddStringToObject(root, "type", type);
    cJSON_AddNumberToObject(root, "val", val);
    time_t now; time(&now);
    cJSON_AddNumberToObject(root, "ts", (long)now);

    char *out = cJSON_PrintUnformatted(root);
    if (!out) {
        cJSON_Delete(root);
        return;
    }

    if (sys.mqtt_connected) {
        esp_mqtt_client_publish(mqtt_client, topic_event, out, 0, 1, 0);
    } else {
        sd_save_offline(topic_event, out);
    }

    free(out);
    cJSON_Delete(root);
    ESP_LOGI("NET", "Alarm wysłany. Wysyłam teraz pełny snapshot danych...");
    publish_data();
}

void sync_offline_data() {
    if (!sys.sd_mounted || !sys.mqtt_connected) return;

    if (xSemaphoreTake(sd_mutex, pdMS_TO_TICKS(500))) {
        // Sprawdzamy czy plik w ogóle istnieje
        struct stat st;
        if (stat("/sdcard/sync.txt", &st) != 0) {
            xSemaphoreGive(sd_mutex);
            return; // Plik nie istnieje, nic do roboty
        }

        FILE* f = fopen("/sdcard/sync.txt", "r");
        if (f == NULL) {
            xSemaphoreGive(sd_mutex);
            return;
        }

        ESP_LOGW("SYNC", "Znalazłem plik sync.txt (%ld bajtów). Start wysyłki...", st.st_size);
        
        char line[2048];
        int count = 0;
        int failed = 0;

        while (fgets(line, sizeof(line), f)) {
            line[strcspn(line, "\r\n")] = 0;

            char *separator = strchr(line, '|');
            if (separator != NULL) {
                *separator = '\0';
                char *topic = line;
                char *payload = separator + 1;

                int msg_id = esp_mqtt_client_publish(mqtt_client, topic, payload, 0, 1, 0);
                
                if (msg_id >= 0) {
                    count++;
                    vTaskDelay(pdMS_TO_TICKS(200)); 
                } else {
                    failed++;
                    ESP_LOGE("SYNC", "Błąd wysyłki linii %d", count + failed);
                }
            }
        }
        fclose(f);
        
        if (failed == 0) {
            unlink("/sdcard/sync.txt"); 
            ESP_LOGI("SYNC", "Wysłano pomyślnie %d wiadomości. Plik usunięty.", count);
        } else {
            ESP_LOGW("SYNC", "Wysłano %d, ale %d zawiodło. Zachowuję plik.", count, failed);
        }
        
        xSemaphoreGive(sd_mutex);
    }
}