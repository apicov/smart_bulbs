idf_component_register(SRCS "RemoteDebug.cpp"
                       INCLUDE_DIRS "."
                       REQUIRES "bulb_ctrl" "esp_ringbuf" "log" "support"
                       PRIV_REQUIRES "esp_timer" "espcoredump" "spi_flash")
