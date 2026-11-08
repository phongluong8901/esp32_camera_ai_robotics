idf_component_register(SRCS "main.cpp"
                       INCLUDE_DIRS "."
                       REQUIRES bt esp_wifi esp_http_server nvs_flash esp_psram)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-missing-field-initializers)


