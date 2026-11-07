idf_component_register(SRCS "OtaUpdater.cpp"
                       INCLUDE_DIRS "."
                       REQUIRES "support"
                       PRIV_REQUIRES "app_update" "esp_app_format" "esp_http_client" "esp_https_ota" "esp_timer"
                                     "mbedtls")
