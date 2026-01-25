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


#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "page.h" 

static esp_mqtt_client_handle_t mqtt_client = NULL;
static httpd_handle_t server_handle = NULL;
uint8_t ble_addr_type;

// --- WIFI & MQTT UTILS ---
void url_decode(char *src, char *dst) { char a, b; while (*src) { if ((*src == '%') && ((a = src[1]) && (b = src[2])) && (isxdigit((int)a) && isxdigit((int)b))) { if (a >= 'a') a -= 'a' - 'A'; if (a >= 'A') a -= ('A' - 10); else a -= '0'; if (b >= 'a') b -= 'a' - 'A'; if (b >= 'A') b -= ('A' - 10); else b -= '0'; *dst++ = 16 * a + b; src += 3; } else if (*src == '+') { *dst++ = ' '; src++; } else { *dst++ = *src++; } } *dst = '\0'; }

esp_err_t load_wifi_conf() {
    nvs_handle_t h; if(nvs_open("cfg", NVS_READONLY, &h)!=ESP_OK) return ESP_FAIL;
    size_t l=32; nvs_get_str(h,"ssid",conf_ssid,&l); l=64; nvs_get_str(h,"pass",conf_pass,&l);
    l=32; nvs_get_str(h,"user",conf_user,&l); l=128; nvs_get_str(h,"mqtt",conf_mqtt,&l); nvs_close(h); return ESP_OK;
}

void setup_sntp() { esp_sntp_setoperatingmode(SNTP_OPMODE_POLL); esp_sntp_setservername(0, "pool.ntp.org"); esp_sntp_init(); setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1); tzset(); }

static void wifi_handler(void* arg, esp_event_base_t b, int32_t id, void* d) { if (id == WIFI_EVENT_STA_START) esp_wifi_connect(); else if (id == WIFI_EVENT_STA_DISCONNECTED) { sys.wifi_connected = false; esp_wifi_connect(); } else if (id == IP_EVENT_STA_GOT_IP) { sys.wifi_connected = true; setup_sntp(); } }

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    esp_mqtt_event_handle_t event = event_data;
    
    if (event_id == MQTT_EVENT_CONNECTED) {
        sys.mqtt_connected = true;
        esp_mqtt_client_subscribe(mqtt_client, topic_cmd, 1);
        ESP_LOGI("NET", "MQTT Connected");
    } else if (event_id == MQTT_EVENT_DISCONNECTED) {
        sys.mqtt_connected = false;
    } else if (event_id == MQTT_EVENT_DATA) {
        char data_buf[1024]; // Zwiększony bufor dla dużego JSONa
        int len = event->data_len > sizeof(data_buf) - 1 ? sizeof(data_buf) - 1 : event->data_len;
        memcpy(data_buf, event->data, len);
        data_buf[len] = '\0';

        cJSON *json = cJSON_Parse(data_buf);
        if (!json) return;

        bool config_changed = false;

        // 1. ARM/DISARM
        cJSON *set_item = cJSON_GetObjectItem(json, "set");
        if (cJSON_IsString(set_item)) {
            if (strcmp(set_item->valuestring, "ARM") == 0) sys.is_armed = true;
            else if (strcmp(set_item->valuestring, "DISARM") == 0) sys.is_armed = false;
            save_persistent_state();
        }

        // 2. PEŁNA KONFIGURACJA
        cJSON *cfg = cJSON_GetObjectItem(json, "config");
        if (cfg) {
            cJSON *i;
            // Progi
            if((i = cJSON_GetObjectItem(cfg, "shock_threshold_g"))) current_config.shock_threshold_g = i->valuedouble;
            if((i = cJSON_GetObjectItem(cfg, "temp_min_c")))       current_config.temp_min_c = i->valuedouble;
            if((i = cJSON_GetObjectItem(cfg, "temp_max_c")))       current_config.temp_max_c = i->valuedouble;
            if((i = cJSON_GetObjectItem(cfg, "hum_max_percent")))  current_config.hum_max_percent = i->valuedouble;
            if((i = cJSON_GetObjectItem(cfg, "pres_min_hpa")))     current_config.pres_min_hpa = i->valuedouble;
            if((i = cJSON_GetObjectItem(cfg, "pres_max_hpa")))     current_config.pres_max_hpa = i->valuedouble;
            if((i = cJSON_GetObjectItem(cfg, "bat_min_v")))        current_config.bat_min_v = i->valuedouble;
            if((i = cJSON_GetObjectItem(cfg, "lux_min"))) current_config.lux_min = i->valuedouble;
            if((i = cJSON_GetObjectItem(cfg, "lux_max"))) current_config.lux_max = i->valuedouble;

            // Włączniki sensorów
            if((i = cJSON_GetObjectItem(cfg, "shock_alarm_enabled"))) current_config.shock_alarm_enabled = cJSON_IsTrue(i);
            if((i = cJSON_GetObjectItem(cfg, "temp_alarm_enabled")))  current_config.temp_alarm_enabled = cJSON_IsTrue(i);
            if((i = cJSON_GetObjectItem(cfg, "hum_alarm_enabled")))   current_config.hum_alarm_enabled = cJSON_IsTrue(i);
            if((i = cJSON_GetObjectItem(cfg, "pres_alarm_enabled")))  current_config.pres_alarm_enabled = cJSON_IsTrue(i);
            if((i = cJSON_GetObjectItem(cfg, "bat_alarm_enabled")))   current_config.bat_alarm_enabled = cJSON_IsTrue(i);
            if((i = cJSON_GetObjectItem(cfg, "light_alarm_enabled"))) current_config.light_alarm_enabled = cJSON_IsTrue(i);

            // Włączniki akcji 
            if((i = cJSON_GetObjectItem(cfg, "action_buzzer_enabled"))) current_config.action_buzzer_enabled = cJSON_IsTrue(i);
            if((i = cJSON_GetObjectItem(cfg, "action_motor_enabled")))  current_config.action_motor_enabled = cJSON_IsTrue(i);
            if((i = cJSON_GetObjectItem(cfg, "action_led_enabled")))    current_config.action_led_enabled = cJSON_IsTrue(i);
            if((i = cJSON_GetObjectItem(cfg, "stealth_mode_enabled")))  current_config.stealth_mode_enabled = cJSON_IsTrue(i);

            // System
            if((i = cJSON_GetObjectItem(cfg, "status_interval_sec"))) current_config.status_interval_sec = i->valueint;
            
            config_changed = true;
            ESP_LOGI("NET", "Zaktualizowano konfiguracje MQTT");
        }
        
        cJSON_Delete(json);
        if (config_changed) save_device_config();
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
    char b[256]; int r = httpd_req_recv(req, b, req->content_len); if (r <= 0) return ESP_FAIL; b[r] = 0;
    char sr[32]={0}, pr[64]={0}, ur[32]={0}, mr[128]={0}, s[32], p[64], u[32], m[128];
    char *ptr = strstr(b, "ssid="); if(ptr) sscanf(ptr+5, "%[^&]", sr); ptr = strstr(b, "pass="); if(ptr) sscanf(ptr+5, "%[^&]", pr);
    ptr = strstr(b, "uid=");  if(ptr) sscanf(ptr+4, "%[^&]", ur); ptr = strstr(b, "mqtt="); if(ptr) sscanf(ptr+5, "%[^&]", mr);
    url_decode(sr, s); url_decode(pr, p); url_decode(ur, u); url_decode(mr, m);
    nvs_handle_t h; nvs_open("cfg", NVS_READWRITE, &h); nvs_set_str(h, "ssid", s); nvs_set_str(h, "pass", p); nvs_set_str(h, "user", u); nvs_set_str(h, "mqtt", m); nvs_commit(h); nvs_close(h);
    httpd_resp_send(req, "Saved. Rebooting...", -1); vTaskDelay(pdMS_TO_TICKS(1500)); esp_restart(); return ESP_OK;
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
    if (sys.sd_mounted) sd_log_event("STATUS_UPDATE", sys.temp);
    if (!sys.mqtt_connected) return;
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "temp", sys.temp); cJSON_AddNumberToObject(root, "hum", sys.hum); cJSON_AddNumberToObject(root, "pres", sys.pres);
    cJSON_AddNumberToObject(root, "bat", sys.battery_voltage); cJSON_AddNumberToObject(root, "ax", sys.acc_x);
    cJSON_AddNumberToObject(root, "ay", sys.acc_y); cJSON_AddNumberToObject(root, "az", sys.acc_z); cJSON_AddNumberToObject(root, "g", sys.acc_mag);
    cJSON_AddBoolToObject(root, "armed", sys.is_armed); cJSON_AddNumberToObject(root, "alarms", sys.alarm_count);
    time_t now; time(&now); cJSON_AddNumberToObject(root, "ts", (long)now);
    char *out = cJSON_PrintUnformatted(root); esp_mqtt_client_publish(mqtt_client, topic_data, out, 0, 1, 0); free(out); cJSON_Delete(root);
}
void publish_event(const char* type, float val) {
    sd_log_event(type, val); if (!sys.mqtt_connected) return;
    cJSON *root = cJSON_CreateObject(); cJSON_AddStringToObject(root, "type", type); cJSON_AddNumberToObject(root, "val", val);
    time_t now; time(&now); cJSON_AddNumberToObject(root, "ts", (long)now);
    char *out = cJSON_PrintUnformatted(root); esp_mqtt_client_publish(mqtt_client, topic_event, out, 0, 1, 0); free(out); cJSON_Delete(root);
}

// --- BLE ---
static int device_info_cb(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg) {
    char info[64]; sprintf(info, "ARMED:%d ALARMS:%ld BAT:%.1fV", sys.is_armed, sys.alarm_count, sys.battery_voltage);
    os_mbuf_append(ctxt->om, info, strlen(info)); return 0;
}
static const struct ble_gatt_svc_def gatt_svcs[] = {
    { .type = BLE_GATT_SVC_TYPE_PRIMARY, .uuid = BLE_UUID128_DECLARE(0xFB,0x34,0x9B,0x5F,0x80,0x00,0x00,0x80,0x00,0x10,0x00,0x00,0xFF,0x00,0x00,0x00),
      .characteristics = (struct ble_gatt_chr_def[]) { { .uuid = BLE_UUID128_DECLARE(0xFB,0x34,0x9B,0x5F,0x80,0x00,0x00,0x80,0x00,0x10,0x00,0x00,0x01,0xFF,0x00,0x00), .flags = BLE_GATT_CHR_F_READ, .access_cb = device_info_cb }, { 0 } }, }, { 0 },
};
void ble_app_advertise(void) {
    ble_gap_adv_stop(); struct ble_gap_adv_params adv_params; struct ble_hs_adv_fields fields; memset(&fields, 0, sizeof fields);
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    static char name_buf[32];
    if (is_setup_mode) sprintf(name_buf, "GUARD_SETUP"); else if (sys.is_armed) sprintf(name_buf, "G:ON A:%ld", sys.alarm_count); else sprintf(name_buf, "G:OFF B:%.1f", sys.battery_voltage);
    fields.name = (uint8_t *)name_buf; fields.name_len = strlen(name_buf); fields.name_is_complete = 1;
    ble_gap_adv_set_fields(&fields); ble_hs_id_infer_auto(0, &ble_addr_type);
    memset(&adv_params, 0, sizeof adv_params); adv_params.conn_mode = BLE_GAP_CONN_MODE_UND; adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    ble_gap_adv_start(ble_addr_type, NULL, BLE_HS_FOREVER, &adv_params, NULL, NULL);
}
void ble_app_on_sync(void) { ble_hs_util_ensure_addr(0); ble_hs_id_infer_auto(0, &ble_addr_type); }
void ble_host_task(void *param) { nimble_port_run(); nimble_port_freertos_deinit(); }
void setup_ble_nimble() { nimble_port_init(); ble_svc_gap_init(); ble_svc_gatt_init(); ble_gatts_count_cfg(gatt_svcs); ble_gatts_add_svcs(gatt_svcs); ble_hs_cfg.sync_cb = ble_app_on_sync; nimble_port_freertos_init(ble_host_task); }