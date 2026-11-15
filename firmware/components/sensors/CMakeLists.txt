idf_component_register(SRCS "SensorHub.cpp" "SensorReport.cpp" "Sensors.cpp"
                       INCLUDE_DIRS "."
                       REQUIRES "support" "esp_driver_gpio" "esp_driver_i2c" "freertos"
                                "i2cdev" "veml7700" "tsl2591" "scd30" "vl53l1x_library")
