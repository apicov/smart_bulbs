# Unit tests for the firmware's hardware-independent logic, run on the PC.
#   cmake -S tests/host -B tests/host/build && cmake --build tests/host/build && ctest --test-dir tests/host/build
cmake_minimum_required(VERSION 3.16)
project(pico_bulb_host_tests C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(FIRMWARE ${CMAKE_CURRENT_LIST_DIR}/../..)
set(CJSON ${FIRMWARE}/managed_components/espressif__cjson/cJSON)
if(NOT EXISTS ${CJSON}/cJSON.c)
    message(FATAL_ERROR "cJSON not found: run `idf.py build` once to download managed_components")
endif()

add_executable(host_tests
    test_main.cpp
    ${FIRMWARE}/components/bulb_ctrl/BulbProtocol.cpp
    ${CJSON}/cJSON.c)
target_include_directories(host_tests PRIVATE
    ${FIRMWARE}/components/bulb_config
    ${FIRMWARE}/components/bulb_ctrl
    ${FIRMWARE}/components/light_engine
    ${FIRMWARE}/components/ota_updater
    ${FIRMWARE}/components/support
    ${CJSON})
# Trap mode needs no sanitizer runtime library to be installed.
target_compile_options(host_tests PRIVATE -Wall -fsanitize=undefined -fsanitize-undefined-trap-on-error)
target_compile_definitions(host_tests PRIVATE _GLIBCXX_ASSERTIONS)

enable_testing()
add_test(NAME host_tests COMMAND host_tests)
