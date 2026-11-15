idf_component_register(SRCS "BulbNode.cpp"
                       INCLUDE_DIRS "."
                       REQUIRES "bulb_config" "bulb_ctrl" "light_engine" "mqtt_client" "ota_updater" "pir"
                                "remote_debug" "support" "wifi_station"
                       PRIV_REQUIRES "nvs_flash" "esp_hw_support" "esp_netif" "esp_timer")
