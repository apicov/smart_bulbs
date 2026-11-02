cmake_minimum_required(VERSION 3.16)

# Secrets (WiFi/MQTT passwords) live in the git-ignored sdkconfig.defaults.local
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults.local")
    set(SDKCONFIG_DEFAULTS "sdkconfig.defaults;sdkconfig.defaults.local")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
idf_build_set_property(MINIMAL_BUILD ON)
project(pico_bulb)
