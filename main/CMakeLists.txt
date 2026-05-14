set(LV_DEMO_DIR ../managed_components/lvgl__lvgl/demos)
file(GLOB_RECURSE LV_DEMOS_SOURCES ${LV_DEMO_DIR}/*.c)

idf_component_register(
    SRCS main.c stm32_interface.c ui_standby.c ui_motor.c ui_misc.c ui_nfc.c ui_sys_stats.c ui_ble.c ui_manager.c ble_control.c crc.c wifi_prov.c ui_wifi_prov.c ${LV_DEMOS_SOURCES}
    INCLUDE_DIRS . ${LV_DEMO_DIR}
    REQUIRES bt nvs_flash spi_flash esp_psram esp_driver_uart esp_driver_gpio esp_hw_support esp_pm esp_driver_tsens waveshare__esp32_s3_touch_amoled_1_75 esp_wifi esp_netif esp_http_server esp_event)

idf_component_get_property(LVGL_LIB lvgl__lvgl COMPONENT_LIB)
target_compile_options(
    ${LVGL_LIB} 
    PRIVATE
        -DLV_LVGL_H_INCLUDE_SIMPLE
        -DLV_USE_DEMO_MUSIC
)
