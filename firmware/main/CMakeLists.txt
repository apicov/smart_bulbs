idf_component_register(SRCS "main.cpp"
                       REQUIRES "bulb_ctrl" "light_engine" "mqtt_client" "pir" "wifi_station"
                                "nvs_flash" "esp_hw_support")
