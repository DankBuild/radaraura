idf_component_register(
    SRCS main.c ui.c
    INCLUDE_DIRS .
    PRIV_REQUIRES bsp_extra esp_timer driver spiffs
                  espressif__esp32_p4_function_ev_board lvgl__lvgl)

idf_component_get_property(LVGL_LIB lvgl__lvgl COMPONENT_LIB)
target_compile_options(
    ${LVGL_LIB} 
    PRIVATE 
        -DLV_LVGL_H_INCLUDE_SIMPLE
        -DLV_USE_DEMO_MUSIC
)
