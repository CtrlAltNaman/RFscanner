idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES
        buttons
        csi
        drivers
        nrf24
        rf
        spi_manager
        system
        tft_display
        ui
        nvs_flash
)
