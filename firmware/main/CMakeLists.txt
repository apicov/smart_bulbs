idf_component_register(SRCS "main.cpp"
                       REQUIRES "bulb_config" "bulb_ctrl" "light_engine" "mqtt_client" "ota_updater" "pir" "support" "wifi_station"
                                "nvs_flash" "esp_hw_support" "esp_netif" "espcoredump")
