idf_component_register(SRCS "main.cpp" "tb6612.cpp"
                       INCLUDE_DIRS "." "../include"
                       REQUIRES driver esp_driver_gpio esp_driver_ledc)
