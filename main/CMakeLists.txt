set(LV_DEMO_DIR ../managed_components/lvgl__lvgl/demos)

idf_component_register(
    SRCS main.c stm32_interface.c test_flow.c nano_api.c ui_standby.c ui_motor.c ui_misc.c ui_nfc.c ui_sys_stats.c ui_ble.c ui_manager.c crc.c wifi_prov.c ui_wifi_prov.c
    INCLUDE_DIRS . ${LV_DEMO_DIR}
    REQUIRES nvs_flash spi_flash esp_psram esp_driver_uart esp_driver_gpio esp_hw_support esp_app_format esp_pm esp_driver_tsens waveshare__esp32_s3_touch_amoled_1_75 esp_wifi esp_netif esp_http_server esp_http_client esp-tls esp_event json)

idf_component_get_property(LVGL_LIB lvgl__lvgl COMPONENT_LIB)
target_compile_options(
    ${LVGL_LIB} 
    PRIVATE
        -DLV_LVGL_H_INCLUDE_SIMPLE
        -DLV_USE_DEMO_MUSIC
)
