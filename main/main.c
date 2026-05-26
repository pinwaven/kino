#include <stdio.h>
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
#include "driver/i2c_master.h"

static const char *TAG = "APP_MAIN";
static const char *AXP2101_TAG = "AXP2101";

#define AXP2101_I2C_ADDR 0x34
#define AXP2101_DC_UVP_POWEROFF_MASK 0x1F
#define BOOT_BRIGHTNESS_START 18
#define BOOT_BRIGHTNESS_TARGET 75

static esp_err_t axp2101_read_reg(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t *value)
{
    return i2c_master_transmit_receive(dev, &reg, sizeof(reg), value, sizeof(*value), 100);
}

static esp_err_t axp2101_write_reg(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t value)
{
    uint8_t data[] = {reg, value};
    return i2c_master_transmit(dev, data, sizeof(data), 100);
}

static esp_err_t axp2101_update_bits(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t mask, uint8_t value)
{
    uint8_t old_value = 0;
    esp_err_t ret = axp2101_read_reg(dev, reg, &old_value);
    if (ret != ESP_OK) {
        return ret;
    }

    return axp2101_write_reg(dev, reg, (old_value & ~mask) | (value & mask));
}

static esp_err_t __attribute__((unused)) axp2101_pmu_restart(i2c_master_dev_handle_t dev)
{
    return axp2101_update_bits(dev, 0x10, 0x02, 0x02);
}

static esp_err_t __attribute__((unused)) axp2101_soft_poweroff(i2c_master_dev_handle_t dev)
{
    return axp2101_update_bits(dev, 0x10, 0x01, 0x01);
}

static void axp2101_disable_dcdc_uvp_poweroff(i2c_master_dev_handle_t dev)
{
    uint8_t old_value = 0;
    esp_err_t ret = axp2101_read_reg(dev, 0x23, &old_value);
    if (ret != ESP_OK) {
        ESP_LOGW(AXP2101_TAG, "read REG23 before UVP disable failed: %s", esp_err_to_name(ret));
        return;
    }

    ret = axp2101_update_bits(dev, 0x23, AXP2101_DC_UVP_POWEROFF_MASK, 0);
    if (ret != ESP_OK) {
        ESP_LOGW(AXP2101_TAG, "disable DCDC UVP poweroff failed: %s", esp_err_to_name(ret));
        return;
    }

    uint8_t new_value = 0;
    ret = axp2101_read_reg(dev, 0x23, &new_value);
    if (ret == ESP_OK) {
        ESP_LOGW(AXP2101_TAG,
                 "DCDC UVP poweroff disabled for test: REG23 0x%02X -> 0x%02X (OVP bit kept)",
                 old_value,
                 new_value);
    }
}

static void axp2101_dump_key_reg(i2c_master_dev_handle_t dev, uint8_t reg, const char *name)
{
    uint8_t value = 0;
    esp_err_t ret = axp2101_read_reg(dev, reg, &value);
    if (ret == ESP_OK) {
        ESP_LOGI(AXP2101_TAG, "key 0x%02X %-18s = 0x%02X", reg, name, value);
    } else {
        ESP_LOGW(AXP2101_TAG, "key 0x%02X %-18s read failed: %s", reg, name, esp_err_to_name(ret));
    }
}

static const char *axp2101_battery_direction_str(uint8_t status2)
{
    switch ((status2 >> 5) & 0x03) {
    case 0:
        return "standby";
    case 1:
        return "charge";
    case 2:
        return "discharge";
    default:
        return "reserved";
    }
}

static const char *axp2101_charge_status_str(uint8_t status2)
{
    switch (status2 & 0x07) {
    case 0:
        return "tri_charge";
    case 1:
        return "pre_charge";
    case 2:
        return "constant_current";
    case 3:
        return "constant_voltage";
    case 4:
        return "charge_done";
    case 5:
        return "not_charging";
    default:
        return "reserved";
    }
}

static uint16_t axp2101_dcdc1_mv(uint8_t reg82)
{
    return (uint16_t)((reg82 & 0x1F) * 100 + 1500);
}

static uint16_t axp2101_dcdc23_mv(uint8_t value)
{
    value &= 0x7F;
    if (value < 71) {
        return (uint16_t)(500 + value * 10);
    }
    if (value < 88) {
        return (uint16_t)(1220 + (value - 71) * 20);
    }
    return (uint16_t)(1600 + (value - 88) * 100);
}

static uint16_t axp2101_dcdc4_mv(uint8_t value)
{
    value &= 0x7F;
    if (value < 71) {
        return (uint16_t)(500 + value * 10);
    }
    return (uint16_t)(1220 + (value - 71) * 20);
}

static void axp2101_log_poweroff_status(uint8_t status)
{
    ESP_LOGI(AXP2101_TAG,
             "PWROFF_STATUS=0x%02X over_temp=%u dc_ov=%u dc_uv=%u vbus_ov=%u vsys_uv=%u sw_off=%u pwron_off=%u",
             status,
             (unsigned)((status >> 7) & 1),
             (unsigned)((status >> 6) & 1),
             (unsigned)((status >> 5) & 1),
             (unsigned)((status >> 4) & 1),
             (unsigned)((status >> 3) & 1),
             (unsigned)((status >> 2) & 1),
             (unsigned)((status >> 1) & 1));
}

static void axp2101_decode_boot_registers(i2c_master_dev_handle_t dev)
{
    uint8_t r00 = 0, r01 = 0, r10 = 0, r15 = 0, r16 = 0, r18 = 0, r19 = 0;
    uint8_t r20 = 0, r21 = 0, r22 = 0, r23 = 0, r24 = 0, r25 = 0, r26 = 0, r27 = 0;
    uint8_t r80 = 0, r81 = 0, r82 = 0, r83 = 0, r84 = 0, r85 = 0, r90 = 0, r91 = 0;

    if (axp2101_read_reg(dev, 0x00, &r00) != ESP_OK ||
        axp2101_read_reg(dev, 0x01, &r01) != ESP_OK ||
        axp2101_read_reg(dev, 0x10, &r10) != ESP_OK ||
        axp2101_read_reg(dev, 0x15, &r15) != ESP_OK ||
        axp2101_read_reg(dev, 0x16, &r16) != ESP_OK ||
        axp2101_read_reg(dev, 0x18, &r18) != ESP_OK ||
        axp2101_read_reg(dev, 0x19, &r19) != ESP_OK ||
        axp2101_read_reg(dev, 0x20, &r20) != ESP_OK ||
        axp2101_read_reg(dev, 0x21, &r21) != ESP_OK ||
        axp2101_read_reg(dev, 0x22, &r22) != ESP_OK ||
        axp2101_read_reg(dev, 0x23, &r23) != ESP_OK ||
        axp2101_read_reg(dev, 0x24, &r24) != ESP_OK ||
        axp2101_read_reg(dev, 0x25, &r25) != ESP_OK ||
        axp2101_read_reg(dev, 0x26, &r26) != ESP_OK ||
        axp2101_read_reg(dev, 0x27, &r27) != ESP_OK ||
        axp2101_read_reg(dev, 0x80, &r80) != ESP_OK ||
        axp2101_read_reg(dev, 0x81, &r81) != ESP_OK ||
        axp2101_read_reg(dev, 0x82, &r82) != ESP_OK ||
        axp2101_read_reg(dev, 0x83, &r83) != ESP_OK ||
        axp2101_read_reg(dev, 0x84, &r84) != ESP_OK ||
        axp2101_read_reg(dev, 0x85, &r85) != ESP_OK ||
        axp2101_read_reg(dev, 0x90, &r90) != ESP_OK ||
        axp2101_read_reg(dev, 0x91, &r91) != ESP_OK) {
        ESP_LOGW(AXP2101_TAG, "decode skipped: required register read failed");
        return;
    }

    ESP_LOGI(AXP2101_TAG,
             "status1 VBUS_GOOD=%u BATFET=%u BAT_PRESENT=%u THERMAL_REG=%u CURRENT_LIMIT=%u",
             (unsigned)((r00 >> 5) & 1),
             (unsigned)((r00 >> 4) & 1),
             (unsigned)((r00 >> 3) & 1),
             (unsigned)((r00 >> 1) & 1),
             (unsigned)(r00 & 1));
    ESP_LOGI(AXP2101_TAG,
             "status2 SYS_ON=%u VINDPM=%u BAT_DIR=%s CHG=%s",
             (unsigned)((r01 >> 4) & 1),
             (unsigned)((r01 >> 3) & 1),
             axp2101_battery_direction_str(r01),
             axp2101_charge_status_str(r01));
    ESP_LOGI(AXP2101_TAG,
             "COMMON=0x%02X off_discharge=%u pwrok_restart=%u pwron_16s_shutdown=%u restart_req=%u softoff_req=%u",
             r10,
             (unsigned)((r10 >> 5) & 1),
             (unsigned)((r10 >> 3) & 1),
             (unsigned)((r10 >> 2) & 1),
             (unsigned)((r10 >> 1) & 1),
             (unsigned)(r10 & 1));
    ESP_LOGI(AXP2101_TAG,
             "PWRON_SRC=0x%02X vbus_good=%u irq_low=%u pwron_key=%u",
             r20,
             (unsigned)((r20 >> 2) & 1),
             (unsigned)((r20 >> 1) & 1),
             (unsigned)(r20 & 1));
    axp2101_log_poweroff_status(r21);
    ESP_LOGI(AXP2101_TAG,
             "PWROFF_EN=0x%02X die_temp_l2=%u pwron_offlevel=%u offlevel_action=%s",
             r22,
             (unsigned)((r22 >> 2) & 1),
             (unsigned)((r22 >> 1) & 1),
             (r22 & 1) ? "restart" : "poweroff");
    ESP_LOGI(AXP2101_TAG,
             "DCDC_UV_OV_POWEROFF=0x%02X hv=%u dc5_uv=%u dc4_uv=%u dc3_uv=%u dc2_uv=%u dc1_uv=%u",
             r23,
             (unsigned)((r23 >> 5) & 1),
             (unsigned)((r23 >> 4) & 1),
             (unsigned)((r23 >> 3) & 1),
             (unsigned)((r23 >> 2) & 1),
             (unsigned)((r23 >> 1) & 1),
             (unsigned)(r23 & 1));
    ESP_LOGI(AXP2101_TAG,
             "limits VINDPM=%umV IINLIM_CODE=%u WATCHDOG=%s timeout_code=%u VOFF=%umV",
             (unsigned)(3880 + (r15 & 0x0F) * 80),
             (unsigned)(r16 & 0x07),
             (r18 & 0x01) ? "enabled" : "disabled",
             (unsigned)(r19 & 0x07),
             (unsigned)(2600 + (r24 & 0x07) * 100));
    ESP_LOGI(AXP2101_TAG,
             "sequence PWROK=0x%02X SLEEP_WAKE=0x%02X IRQ_OFF_ON=0x%02X",
             r25,
             r26,
             r27);
    ESP_LOGI(AXP2101_TAG,
             "DCDC_ENABLE d1=%u d2=%u d3=%u d4=%u d5=%u FORCE_PWM=0x%02X",
             (unsigned)(r80 & 1),
             (unsigned)((r80 >> 1) & 1),
             (unsigned)((r80 >> 2) & 1),
             (unsigned)((r80 >> 3) & 1),
             (unsigned)((r80 >> 4) & 1),
             r81);
    ESP_LOGI(AXP2101_TAG,
             "DCDC_VOLT d1=%umV d2=%umV d3=%umV d4=%umV",
             (unsigned)axp2101_dcdc1_mv(r82),
             (unsigned)axp2101_dcdc23_mv(r83),
             (unsigned)axp2101_dcdc23_mv(r84),
             (unsigned)axp2101_dcdc4_mv(r85));
    ESP_LOGI(AXP2101_TAG, "LDO_ENABLE 90=0x%02X 91=0x%02X", r90, r91);
}

static void axp2101_log_and_clear_irq(i2c_master_dev_handle_t dev)
{
    uint8_t irq0 = 0, irq1 = 0, irq2 = 0;
    if (axp2101_read_reg(dev, 0x48, &irq0) != ESP_OK ||
        axp2101_read_reg(dev, 0x49, &irq1) != ESP_OK ||
        axp2101_read_reg(dev, 0x4A, &irq2) != ESP_OK) {
        ESP_LOGW(AXP2101_TAG, "irq status read failed");
        return;
    }

    ESP_LOGI(AXP2101_TAG, "IRQ_STATUS_BEFORE_CLEAR 48=%02X 49=%02X 4A=%02X", irq0, irq1, irq2);
    if (irq0) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(axp2101_write_reg(dev, 0x48, irq0));
    }
    if (irq1) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(axp2101_write_reg(dev, 0x49, irq1));
    }
    if (irq2) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(axp2101_write_reg(dev, 0x4A, irq2));
    }
}

static void axp2101_dump_range(i2c_master_dev_handle_t dev, uint8_t start, uint8_t end)
{
    for (uint16_t base = start; base <= end; base += 16) {
        char line[96];
        int pos = snprintf(line, sizeof(line), "%02X:", (unsigned)base);
        if (pos < 0) {
            return;
        }

        for (uint16_t reg = base; reg <= end && reg < base + 16; reg++) {
            uint8_t value = 0;
            esp_err_t ret = axp2101_read_reg(dev, (uint8_t)reg, &value);
            int written = snprintf(line + pos,
                                   sizeof(line) - (size_t)pos,
                                   ret == ESP_OK ? " %02X" : " --",
                                   value);
            if (written < 0) {
                return;
            }
            pos += written;
            if ((size_t)pos >= sizeof(line)) {
                break;
            }
        }

        ESP_LOGI(AXP2101_TAG, "%s", line);
    }
}

static void axp2101_dump_boot_registers(void)
{
    esp_err_t ret = bsp_i2c_init();
    if (ret != ESP_OK) {
        ESP_LOGW(AXP2101_TAG, "i2c init failed before dump: %s", esp_err_to_name(ret));
        return;
    }

    i2c_master_bus_handle_t bus = bsp_i2c_get_handle();
    if (!bus) {
        ESP_LOGW(AXP2101_TAG, "i2c bus unavailable before dump");
        return;
    }

    ret = i2c_master_probe(bus, AXP2101_I2C_ADDR, 100);
    if (ret != ESP_OK) {
        ESP_LOGW(AXP2101_TAG, "not found at 0x%02X: %s", AXP2101_I2C_ADDR, esp_err_to_name(ret));
        return;
    }

    const i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = AXP2101_I2C_ADDR,
        .scl_speed_hz = 100000,
    };
    i2c_master_dev_handle_t dev = NULL;
    ret = i2c_master_bus_add_device(bus, &dev_cfg, &dev);
    if (ret != ESP_OK) {
        ESP_LOGW(AXP2101_TAG, "add device failed: %s", esp_err_to_name(ret));
        return;
    }

    axp2101_disable_dcdc_uvp_poweroff(dev);
    ESP_LOGI(AXP2101_TAG, "boot register dump begin addr=0x%02X diagnostic", AXP2101_I2C_ADDR);
    axp2101_dump_key_reg(dev, 0x00, "mode/charge status");
    axp2101_dump_key_reg(dev, 0x03, "chip id");
    axp2101_dump_key_reg(dev, 0x10, "common config");
    axp2101_dump_key_reg(dev, 0x12, "batfet control");
    axp2101_dump_key_reg(dev, 0x13, "die temp cfg");
    axp2101_dump_key_reg(dev, 0x20, "power-on status");
    axp2101_dump_key_reg(dev, 0x21, "power-off status");
    axp2101_dump_key_reg(dev, 0x22, "power-off enable");
    axp2101_dump_key_reg(dev, 0x23, "dcdc ovp/uvp off");
    axp2101_dump_key_reg(dev, 0x48, "irq status1");
    axp2101_dump_key_reg(dev, 0x80, "dcdc enable");
    axp2101_dump_key_reg(dev, 0x90, "ldo enable");
    axp2101_decode_boot_registers(dev);
    axp2101_log_and_clear_irq(dev);
    axp2101_dump_range(dev, 0x00, 0x4F);
    axp2101_dump_range(dev, 0x80, 0x9F);
    ESP_LOGI(AXP2101_TAG, "boot register dump end");

    ret = i2c_master_bus_rm_device(dev);
    if (ret != ESP_OK) {
        ESP_LOGW(AXP2101_TAG, "remove device failed: %s", esp_err_to_name(ret));
    }
}

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
    axp2101_dump_boot_registers();

    // 1. 立即启动屏幕背光与 LVGL 底层引擎
    ESP_LOGI(TAG, "display start");
    lv_display_t *disp = bsp_display_start();
    if (!disp) {
        ESP_LOGE(TAG, "display start failed");
        return;
    }
    ESP_LOGI(TAG, "display ready");
    vTaskDelay(pdMS_TO_TICKS(50));

    ESP_LOGI(TAG, "brightness set low for boot animation");
    bsp_display_brightness_set(BOOT_BRIGHTNESS_START);

    ESP_LOGI(TAG, "display lock");
    bsp_display_lock(-1);

    // 2. 加载并展示 3 秒优雅的启动动画
    ESP_LOGI(TAG, "show boot animation");
    show_boot_animation();
    ESP_LOGI(TAG, "boot animation ready");

    bsp_display_unlock();

    // 3. 保持屏幕运行动画，主任务在此阻塞等待 3 秒
    vTaskDelay(pdMS_TO_TICKS(3000));
    bsp_display_brightness_set(BOOT_BRIGHTNESS_TARGET);

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
