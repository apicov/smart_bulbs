idf_component_register(SRCS "BulbCtrl.cpp" "BulbProtocol.cpp"
                       INCLUDE_DIRS "."
                       REQUIRES "light_engine" "nvs_flash" "esp_timer" "support")
