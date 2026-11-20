idf_component_register(
    SRCS "display.c"
    INCLUDE_DIRS "include"
    REQUIRES board driver esp_lcd esp_lcd_sh8601 esp_timer
)
