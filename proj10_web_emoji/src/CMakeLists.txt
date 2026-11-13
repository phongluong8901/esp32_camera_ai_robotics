# This file was automatically generated for projects
# without default 'CMakeLists.txt' file.

FILE(GLOB_RECURSE app_sources ${CMAKE_SOURCE_DIR}/src/*.*)

# Cách 1: Liệt kê trực tiếp từng file trong thư mục src/
idf_component_register(
    SRCS "main.cpp" "lcd_emoji.cpp" "web_server.cpp"
    INCLUDE_DIRS "."
    REQUIRES driver esp_lcd esp_wifi esp_http_server nvs_flash esp_netif esp_event
)