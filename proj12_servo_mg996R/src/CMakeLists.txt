# This file was automatically generated for projects
# without default 'CMakeLists.txt' file.

FILE(GLOB_RECURSE app_sources ${CMAKE_SOURCE_DIR}/src/*.*)

idf_component_register(
    SRCS "main.c" "servo_ctrl.c" "web_server.c"
    INCLUDE_DIRS "."
    REQUIRES driver esp_wifi esp_http_server nvs_flash esp_netif esp_event
)
