idf_component_register(SRCS "main.cpp" "pacman_engine.cpp" "tba_network.cpp" "matrix_display.cpp"
                    INCLUDE_DIRS "."
                    REQUIRES esp_hub75 esp_lcd adafruit_gfx
                    PRIV_REQUIRES 
                    nvs_flash 
                    esp_wifi 
                    esp_event 
                    esp_netif 
                    lwip 
                    driver
                    esp_timer
                    bt 
                    wifi_provisioning 
                    json
                    esp_http_client
                    mbedtls)
