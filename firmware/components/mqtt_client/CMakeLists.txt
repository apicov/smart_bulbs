idf_component_register(SRCS "MQTTClient.cpp"
                       INCLUDE_DIRS "."
                       REQUIRES "esp_event" "mbedtls" "support")
