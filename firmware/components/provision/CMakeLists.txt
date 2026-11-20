idf_component_register(
    SRCS "provision.c" "provision_extra.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "."
    REQUIRES board settings app esp_wifi esp_http_server esp_netif esp_event lwip display audio sd app_update queue
)
