idf_component_register(
    SRCS "board.c"
    INCLUDE_DIRS "include"
    REQUIRES driver esp_io_expander esp_io_expander_tca9554
)
