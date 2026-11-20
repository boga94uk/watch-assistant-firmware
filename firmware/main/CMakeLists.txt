idf_component_register(
    SRCS "app_main.c"
    INCLUDE_DIRS "."
    REQUIRES board nvs_flash esp_event esp_netif settings audio ui display input app net provision console_hw sd queue app_update
)
