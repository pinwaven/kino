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
#include "nano_api.h"
#include "driver/gpio.h"

static const char *TAG = "APP_MAIN";

static const char *reset_reason_to_str(esp_reset_reason_t reason)
{
    switch (reason) {
    case ESP_RST_POWERON:
        return "POWERON";
    case ESP_RST_EXT:
        return "EXT";
    case ESP_RST_SW:
        return "SW";
    case ESP_RST_PANIC:
        return "PANIC";
    case ESP_RST_INT_WDT:
        return "INT_WDT";
    case ESP_RST_TASK_WDT:
        return "TASK_WDT";
    case ESP_RST_WDT:
        return "WDT";
    case ESP_RST_DEEPSLEEP:
        return "DEEPSLEEP";
    case ESP_RST_BROWNOUT:
        return "BROWNOUT";
    case ESP_RST_SDIO:
        return "SDIO";
#if ESP_IDF_VERSION_MAJOR >= 5
    case ESP_RST_USB:
        return "USB";
    case ESP_RST_JTAG:
        return "JTAG";
    case ESP_RST_EFUSE:
        return "EFUSE";
    case ESP_RST_PWR_GLITCH:
        return "PWR_GLITCH";
#endif
    default:
        return "UNKNOWN";
    }
}

static void log_boot_info(void)
{
    esp_chip_info_t chip_info;
    uint32_t flash_size = 0;
    esp_reset_reason_t reset_reason = esp_reset_reason();

    esp_chip_info(&chip_info);
    esp_flash_get_size(NULL, &flash_size);

    ESP_LOGI(TAG,
             "boot reset=%s cores=%d rev=%d flash=%luMB internal=%u dma=%u psram=%u",
             reset_reason_to_str(reset_reason),
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

// 声明字库
LV_FONT_DECLARE(lv_font_montserrat_48);

// 启动动画属性设置回调
static void set_logo_opa_cb(void *var, int32_t val)
{
    lv_obj_set_style_text_opa((lv_obj_t *)var, (lv_opa_t)val, 0);
}

static void set_arc_value_cb(void *var, int32_t val)
{
    lv_arc_set_value((lv_obj_t *)var, val);
}

static void set_arc_rotation_cb(void *var, int32_t val)
{
    lv_arc_set_rotation((lv_obj_t *)var, val);
}

static void set_arc_opa_cb(void *var, int32_t val)
{
    lv_obj_set_style_arc_opa((lv_obj_t *)var, (lv_opa_t)val, LV_PART_MAIN);
    lv_obj_set_style_arc_opa((lv_obj_t *)var, (lv_opa_t)val, LV_PART_INDICATOR);
}

static void show_boot_animation(void)
{
    lv_obj_t *scr = lv_scr_act();
    
    // 1. 设置屏幕背景为暗色调
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0B1C2E), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    // 2. 创建圆弧 (外环进度圈)
    lv_obj_t *arc = lv_arc_create(scr);
    lv_obj_set_size(arc, 180, 180);
    lv_obj_center(arc);
    lv_arc_set_bg_angles(arc, 0, 360);
    lv_arc_set_value(arc, 0);
    
    // 隐藏圆弧自带的滑块 Knob
    lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_style_pad_all(arc, 0, LV_PART_KNOB);
    
    // 设置圆弧线宽和渐变颜色
    lv_obj_set_style_arc_width(arc, 5, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, 5, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc, lv_color_hex(0x13263B), LV_PART_MAIN);       // 暗背景轨道
    lv_obj_set_style_arc_color(arc, lv_color_hex(0x00E5FF), LV_PART_INDICATOR);  // 亮丽青色指示器
    lv_obj_set_style_arc_opa(arc, LV_OPA_0, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(arc, LV_OPA_0, LV_PART_INDICATOR);
    lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);

    // 3. 创建居中 KINO 文本
    lv_obj_t *logo = lv_label_create(scr);
    lv_label_set_text(logo, "KINO");
    lv_obj_set_style_text_font(logo, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(logo, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_letter_space(logo, 6, 0);
    lv_obj_set_style_text_opa(logo, LV_OPA_0, 0);
    lv_obj_align(logo, LV_ALIGN_CENTER, 3, 0);  // 稍微偏移字间距带来的中心错位

    // 4. 配置并启动所有动画序列
    // Logo 渐显 (0 - 800ms)
    lv_anim_t a_logo_in;
    lv_anim_init(&a_logo_in);
    lv_anim_set_var(&a_logo_in, logo);
    lv_anim_set_values(&a_logo_in, LV_OPA_0, LV_OPA_COVER);
    lv_anim_set_time(&a_logo_in, 800);
    lv_anim_set_exec_cb(&a_logo_in, set_logo_opa_cb);
    lv_anim_start(&a_logo_in);

    // Logo 渐隐 (2200 - 3000ms)
    lv_anim_t a_logo_out;
    lv_anim_init(&a_logo_out);
    lv_anim_set_var(&a_logo_out, logo);
    lv_anim_set_values(&a_logo_out, LV_OPA_COVER, LV_OPA_0);
    lv_anim_set_time(&a_logo_out, 800);
    lv_anim_set_delay(&a_logo_out, 2200);
    lv_anim_set_exec_cb(&a_logo_out, set_logo_opa_cb);
    lv_anim_start(&a_logo_out);

    // 圆弧渐显 (0 - 800ms)
    lv_anim_t a_arc_in;
    lv_anim_init(&a_arc_in);
    lv_anim_set_var(&a_arc_in, arc);
    lv_anim_set_values(&a_arc_in, LV_OPA_0, LV_OPA_COVER);
    lv_anim_set_time(&a_arc_in, 800);
    lv_anim_set_exec_cb(&a_arc_in, set_arc_opa_cb);
    lv_anim_start(&a_arc_in);

    // 圆弧进度填满 (0 - 2200ms)
    lv_anim_t a_arc_val;
    lv_anim_init(&a_arc_val);
    lv_anim_set_var(&a_arc_val, arc);
    lv_anim_set_values(&a_arc_val, 0, 100);
    lv_anim_set_time(&a_arc_val, 2200);
    lv_anim_set_exec_cb(&a_arc_val, set_arc_value_cb);
    lv_anim_start(&a_arc_val);

    // 圆弧持续匀速旋转 (0 - 3000ms)
    lv_anim_t a_arc_rot;
    lv_anim_init(&a_arc_rot);
    lv_anim_set_var(&a_arc_rot, arc);
    lv_anim_set_values(&a_arc_rot, 0, 360);
    lv_anim_set_time(&a_arc_rot, 3000);
    lv_anim_set_exec_cb(&a_arc_rot, set_arc_rotation_cb);
    lv_anim_start(&a_arc_rot);

    // 圆弧渐隐 (2200 - 3000ms)
    lv_anim_t a_arc_out;
    lv_anim_init(&a_arc_out);
    lv_anim_set_var(&a_arc_out, arc);
    lv_anim_set_values(&a_arc_out, LV_OPA_COVER, LV_OPA_0);
    lv_anim_set_time(&a_arc_out, 800);
    lv_anim_set_delay(&a_arc_out, 2200);
    lv_anim_set_exec_cb(&a_arc_out, set_arc_opa_cb);
    lv_anim_start(&a_arc_out);
}

void app_main(void)
{
    // 强制关闭板载音频功放以节省 10-30mA 静态功耗
    gpio_config_t amp_conf = {
        .pin_bit_mask = (1ULL << GPIO_NUM_46),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&amp_conf);
    gpio_set_level(GPIO_NUM_46, 0);

    init_cjson_psram_hooks();
    log_boot_info();
    init_nvs_tolerant();

    // 1. 立即启动屏幕背光与 LVGL 底层引擎
    ESP_LOGI(TAG, "display start");
    lv_display_t *disp = bsp_display_start();
    if (!disp) {
        ESP_LOGE(TAG, "display start failed");
        return;
    }
    ESP_LOGI(TAG, "display ready");
    vTaskDelay(pdMS_TO_TICKS(50));

    ESP_LOGI(TAG, "display lock");
    bsp_display_lock(-1);

    ESP_LOGI(TAG, "brightness set");
    bsp_display_brightness_set(50);

    // 2. 加载并展示 3 秒优雅的启动动画
    ESP_LOGI(TAG, "show boot animation");
    show_boot_animation();

    bsp_display_unlock();

    // 3. 保持屏幕运行动画，主任务在此阻塞等待 3 秒
    vTaskDelay(pdMS_TO_TICKS(3000));

    // 4. 动画结束后，执行其余后台硬件及串口初始化
    ESP_LOGI(TAG, "stm32 interface init");
    stm32_interface_init();

    // 5. 初始化主应用程序 UI 并加载主屏幕
    bsp_display_lock(-1);
    ESP_LOGI(TAG, "ui init");
    ui_init();
    ESP_LOGI(TAG, "ui ready");

    bsp_display_unlock();
    ESP_LOGI(TAG, "app main done");
}
