idf_component_register(SRCS "main.c" "hal.c" "network.c" "logic.c" "oledDriver.c"
                    INCLUDE_DIRS "."
                    REQUIRES nvs_flash esp_wifi esp_event esp_http_server mqtt driver esp_adc esp_timer fatfs spiffs sdmmc vfs bt led_strip json)