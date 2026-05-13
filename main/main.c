#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_check.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_memory_utils.h"
#include "esp_system.h"
#include "lvgl.h"
#include "bsp/esp-bsp.h"
#include "bsp/display.h"
#include "stm32_interface.h"
#include "ui_app.h"

static const char *TAG = "APP_MAIN";

static void log_boot_info(void)
{
    esp_chip_info_t chip_info;
    uint32_t flash_size = 0;

    esp_chip_info(&chip_info);
    esp_flash_get_size(NULL, &flash_size);

    ESP_LOGI(TAG,
             "boot reset=%d cores=%d rev=%d flash=%luMB internal=%u dma=%u psram=%u",
             (int)esp_reset_reason(),
             chip_info.cores,
             chip_info.revision,
             (unsigned long)(flash_size / (1024 * 1024)),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
}

static void init_nvs_tolerant(void)
{
    ESP_LOGI(TAG, "nvs init");
    esp_err_t ret = nvs_flash_init();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "nvs init failed: %s, erasing nvs", esp_err_to_name(ret));
        esp_err_t erase_ret = nvs_flash_erase();
        if (erase_ret != ESP_OK) {
            ESP_LOGE(TAG, "nvs erase failed: %s", esp_err_to_name(erase_ret));
            return;
        }
        ret = nvs_flash_init();
    }

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "nvs unavailable after erase: %s", esp_err_to_name(ret));
        return;
    }

    ESP_LOGI(TAG, "nvs ready");
}

void app_main(void)
{
    log_boot_info();
    init_nvs_tolerant();

    // 初始化串口接口
    ESP_LOGI(TAG, "stm32 interface init");
    stm32_interface_init();

    ESP_LOGI(TAG, "display start");
    lv_display_t *disp = bsp_display_start();
    if (!disp) {
        ESP_LOGE(TAG, "display start failed");
        return;
    }
    ESP_LOGI(TAG, "display ready");

    bsp_display_lock(-1);

    bsp_display_brightness_set(50);

    // 初始化自定义 UI
    ui_init();

    bsp_display_unlock();
}
