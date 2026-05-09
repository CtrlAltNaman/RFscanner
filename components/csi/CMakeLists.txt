idf_component_register(
    SRCS "csi_mode.c"
    INCLUDE_DIRS "include"
    REQUIRES system utils nvs_flash esp_wifi esp_event esp_netif esp_common
)
