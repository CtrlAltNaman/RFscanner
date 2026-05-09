idf_component_register(
    SRCS "nrf24.c"
    INCLUDE_DIRS "include"
    REQUIRES driver spi_manager
)
