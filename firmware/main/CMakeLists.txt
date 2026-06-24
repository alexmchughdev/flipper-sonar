idf_component_register(
    SRCS
        "claudeogotchi_main.c"
        "nvs_store.c"
        "wifi.c"
        "uart_link.c"
        "ws_client.c"
    INCLUDE_DIRS "." "../../proto"
    REQUIRES nvs_flash esp_wifi esp_event esp_netif driver json esp-tls mbedtls
)
