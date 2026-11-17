idf_component_register(SRCS "main.c" "dfplayer.c" "call.c" "audio.c"
                       INCLUDE_DIRS "."
                       REQUIRES esp_driver_gpio esp_driver_uart esp_driver_i2s esp_hw_support esp_wifi esp_netif esp_event nvs_flash)
