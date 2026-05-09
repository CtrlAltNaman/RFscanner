idf_component_register(
    SRCS "app_system.c"
    INCLUDE_DIRS "include"
    REQUIRES buttons drivers nrf24 freertos esp_timer nvs_flash utils
)
