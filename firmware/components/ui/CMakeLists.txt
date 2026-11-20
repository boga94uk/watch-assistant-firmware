idf_component_register(
    SRCS "ui_port.c" "ui_screens.c" "ui_galaxy.c"
    INCLUDE_DIRS "include"
    REQUIRES display board esp_timer settings log
)
