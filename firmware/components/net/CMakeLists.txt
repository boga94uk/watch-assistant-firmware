idf_component_register(
    SRCS "net.c" "reply_parse.c"
    INCLUDE_DIRS "include"
    REQUIRES app audio settings queue esp_wifi esp_event esp_netif esp_http_client esp-tls json mbedtls nvs_flash lwip
)
