idf_component_register(
    SRCS "rf_scanner.c"
    INCLUDE_DIRS "include"
    REQUIRES freertos nrf24 system utils esp_timer esp_common
)
