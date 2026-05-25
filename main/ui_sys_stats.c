#include "ui_app.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_flash.h"
#include "esp_timer.h"
#include "esp_clk_tree.h"
#include "esp_private/pm_impl.h"
#include "driver/temperature_sensor.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static lv_obj_t *lbl_heap;
static lv_obj_t *lbl_version;
static lv_obj_t *lbl_psram;
static lv_obj_t *lbl_uptime;
static lv_obj_t *lbl_flash;
static lv_obj_t *lbl_cpu_core0;
static lv_obj_t *lbl_cpu_core1;
static lv_obj_t *lbl_cpu_freq;
static lv_obj_t *lbl_freq_dist;
static lv_obj_t *lbl_temp;
static lv_timer_t *update_timer;
static temperature_sensor_handle_t temp_sensor;
static bool temp_sensor_init_attempted;

static uint32_t prev_idle_ticks[2] = {0, 0};
static uint32_t prev_total_ticks = 0;
typedef struct {
    int64_t bin_us[3];
} freq_dist_sample_t;

static freq_dist_sample_t freq_samples[5];
static size_t freq_sample_pos;
static size_t freq_sample_count;
static int64_t prev_pm_time_us[3];
static bool prev_pm_time_valid;

static const char *app_version_string(void)
{
    const esp_app_desc_t *desc = esp_app_get_description();
    if (desc && desc->version[0] != '\0') {
        return desc->version;
    }
    return __DATE__ " " __TIME__;
}

static size_t cpu_freq_bin(uint32_t freq_mhz)
{
    if (freq_mhz <= 100) {
        return 0;
    }
    if (freq_mhz <= 200) {
        return 1;
    }
    return 2;
}

static bool read_pm_freq_times(int64_t out_us[3])
{
    char *stats_buf = NULL;
    size_t stats_len = 0;
    FILE *stream = open_memstream(&stats_buf, &stats_len);
    if (!stream) {
        return false;
    }

    esp_pm_impl_dump_stats(stream);
    fclose(stream);

    if (!stats_buf) {
        return false;
    }

    memset(out_us, 0, sizeof(int64_t) * 3);

    char *saveptr = NULL;
    char *line = strtok_r(stats_buf, "\n", &saveptr);
    while (line) {
        char mode[16];
        unsigned freq_mhz = 0;
        long long time_us = 0;

        if (sscanf(line, "%15s %u M %lld", mode, &freq_mhz, &time_us) == 3) {
            if (strcmp(mode, "APB_MIN") == 0 ||
                strcmp(mode, "APB_MAX") == 0 ||
                strcmp(mode, "CPU_MAX") == 0 ||
                strcmp(mode, "SLEEP") == 0) {
                out_us[cpu_freq_bin(freq_mhz)] += time_us;
            }
        }

        line = strtok_r(NULL, "\n", &saveptr);
    }

    free(stats_buf);
    return true;
}

static void record_cpu_freq_distribution(void)
{
    int64_t current_us[3] = {0};
    if (!read_pm_freq_times(current_us)) {
        return;
    }

    if (prev_pm_time_valid) {
        freq_dist_sample_t sample = {0};
        for (size_t i = 0; i < 3; i++) {
            sample.bin_us[i] = current_us[i] >= prev_pm_time_us[i] ? current_us[i] - prev_pm_time_us[i] : 0;
        }

        freq_samples[freq_sample_pos] = sample;
        freq_sample_pos = (freq_sample_pos + 1) % (sizeof(freq_samples) / sizeof(freq_samples[0]));
        if (freq_sample_count < sizeof(freq_samples) / sizeof(freq_samples[0])) {
            freq_sample_count++;
        }
    }

    memcpy(prev_pm_time_us, current_us, sizeof(prev_pm_time_us));
    prev_pm_time_valid = true;
}

static void format_freq_distribution(void)
{
    int64_t sum_us[3] = {0};
    int64_t total_us = 0;

    for (size_t i = 0; i < freq_sample_count; i++) {
        for (size_t j = 0; j < 3; j++) {
            sum_us[j] += freq_samples[i].bin_us[j];
            total_us += freq_samples[i].bin_us[j];
        }
    }

    if (total_us <= 0) {
        lv_label_set_text(lbl_freq_dist, "5S DIST: --");
        return;
    }

    lv_label_set_text_fmt(lbl_freq_dist,
                          "5S DIST: 80:%u%% 160:%u%% 240:%u%%",
                          (unsigned)(sum_us[0] * 100 / total_us),
                          (unsigned)(sum_us[1] * 100 / total_us),
                          (unsigned)(sum_us[2] * 100 / total_us));
}

static void reset_cpu_freq_distribution(void)
{
    memset(freq_samples, 0, sizeof(freq_samples));
    memset(prev_pm_time_us, 0, sizeof(prev_pm_time_us));
    freq_sample_pos = 0;
    freq_sample_count = 0;
    prev_pm_time_valid = false;
}

static void update_cpu_freq_distribution(void)
{
    record_cpu_freq_distribution();
    format_freq_distribution();
}

static void update_cpu_freq_labels(void)
{
    uint32_t freq_hz = 0;
    esp_clk_tree_src_get_freq_hz(SOC_MOD_CLK_CPU, ESP_CLK_TREE_SRC_FREQ_PRECISION_CACHED, &freq_hz);
    uint32_t freq_mhz = freq_hz / 1000000;

    lv_label_set_text_fmt(lbl_cpu_freq, "CPU FREQ: %lu MHz", (unsigned long)freq_mhz);
    update_cpu_freq_distribution();
}

static esp_err_t ensure_temp_sensor(void)
{
    if (temp_sensor) {
        return ESP_OK;
    }

    if (temp_sensor_init_attempted) {
        return ESP_FAIL;
    }
    temp_sensor_init_attempted = true;

    temperature_sensor_config_t temp_config = TEMPERATURE_SENSOR_CONFIG_DEFAULT(-10, 80);
    esp_err_t err = temperature_sensor_install(&temp_config, &temp_sensor);
    if (err != ESP_OK) {
        temp_sensor = NULL;
        return err;
    }

    err = temperature_sensor_enable(temp_sensor);
    if (err != ESP_OK) {
        temperature_sensor_uninstall(temp_sensor);
        temp_sensor = NULL;
    }
    return err;
}

static void update_temperature_label(void)
{
    float temp_c = 0.0f;
    esp_err_t err = ensure_temp_sensor();
    if (err == ESP_OK) {
        err = temperature_sensor_get_celsius(temp_sensor, &temp_c);
    }

    if (err == ESP_OK) {
        lv_label_set_text_fmt(lbl_temp, "TEMP: %.1f C", temp_c);
    } else {
        lv_label_set_text(lbl_temp, "TEMP: --");
    }
}

static void update_stats(lv_timer_t * t) {
    // Memory and Uptime
    size_t free_heap = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    size_t total_heap = heap_caps_get_total_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    size_t free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    size_t total_psram = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    
    int64_t uptime_us = esp_timer_get_time();
    int64_t uptime_s = uptime_us / 1000000;
    
    // CPU Load Calculation
    TaskStatus_t *task_array;
    UBaseType_t task_array_size = uxTaskGetNumberOfTasks();
    uint32_t total_runtime;
    task_array = pvPortMalloc(task_array_size * sizeof(TaskStatus_t));
    
    if (task_array != NULL) {
        task_array_size = uxTaskGetSystemState(task_array, task_array_size, &total_runtime);
        uint32_t idle_ticks[2] = {0, 0};
        for (UBaseType_t i = 0; i < task_array_size; i++) {
            if (strcmp(task_array[i].pcTaskName, "IDLE0") == 0 || strcmp(task_array[i].pcTaskName, "IDLE") == 0) {
                idle_ticks[0] = task_array[i].ulRunTimeCounter;
            } else if (strcmp(task_array[i].pcTaskName, "IDLE1") == 0) {
                idle_ticks[1] = task_array[i].ulRunTimeCounter;
            }
        }
        
        uint32_t total_delta = total_runtime - prev_total_ticks;
        if (total_delta > 0) {
            float usage0 = 100.0f * (1.0f - (float)(idle_ticks[0] - prev_idle_ticks[0]) / (float)total_delta);
            float usage1 = 100.0f * (1.0f - (float)(idle_ticks[1] - prev_idle_ticks[1]) / (float)total_delta);
            
            if (usage0 < 0) { usage0 = 0.0f; }
            if (usage0 > 100) { usage0 = 100.0f; }
            if (usage1 < 0) { usage1 = 0.0f; }
            if (usage1 > 100) { usage1 = 100.0f; }

            lv_label_set_text_fmt(lbl_cpu_core0, "CORE 0 LOAD: %.1f %%", usage0);
            lv_label_set_text_fmt(lbl_cpu_core1, "CORE 1 LOAD: %.1f %%", usage1);
        }
        prev_idle_ticks[0] = idle_ticks[0]; 
        prev_idle_ticks[1] = idle_ticks[1];
        prev_total_ticks = total_runtime;
        vPortFree(task_array);
    }

    lv_label_set_text_fmt(lbl_heap, "SRAM: %d / %d KB", (int)(free_heap / 1024), (int)(total_heap / 1024));
    if (total_psram > 0) {
        lv_label_set_text_fmt(lbl_psram, "PSRAM: %d / %d KB", (int)(free_psram / 1024), (int)(total_psram / 1024));
    } else {
        lv_label_set_text(lbl_psram, "PSRAM: Not Available");
    }
    lv_label_set_text_fmt(lbl_uptime, "UPTIME: %02d:%02d:%02d", (int)(uptime_s/3600), (int)(uptime_s%3600/60), (int)(uptime_s%60));
    update_cpu_freq_labels();
    update_temperature_label();
}

void ui_sys_stats_init(lv_obj_t *tile) {
    lv_obj_set_style_bg_color(tile, lv_color_hex(0x1a1a1a), 0);
    
    lv_obj_t *cont = lv_obj_create(tile);
    lv_obj_set_size(cont, LV_PCT(95), LV_PCT(95));
    lv_obj_set_style_bg_opa(cont, 0, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_align(cont, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(cont, 5, 0);
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);

    lbl_version = lv_label_create(cont);
    lv_label_set_text_fmt(lbl_version, "VER: %s", app_version_string());
    lv_obj_set_style_text_font(lbl_version, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl_version, lv_color_hex(0x95A5A6), 0);

    lv_obj_t *title = lv_label_create(cont);
    lv_label_set_text(title, "SYSTEM MONITOR");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x3498DB), 0);
    lv_obj_set_style_margin_bottom(title, 5, 0);

    // Static info
    uint32_t flash_size; esp_flash_get_size(NULL, &flash_size);

    lbl_flash = lv_label_create(cont);
    lv_label_set_text_fmt(lbl_flash, "FLASH: %d MB", (int)(flash_size / (1024 * 1024)));
    lv_obj_set_style_text_font(lbl_flash, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(lbl_flash, lv_color_hex(0xBDC3C7), 0);

    // Dynamic labels
    lbl_cpu_core0 = lv_label_create(cont);
    lv_obj_set_style_text_font(lbl_cpu_core0, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(lbl_cpu_core0, lv_color_hex(0x3498DB), 0);

    lbl_cpu_core1 = lv_label_create(cont);
    lv_obj_set_style_text_font(lbl_cpu_core1, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(lbl_cpu_core1, lv_color_hex(0x9B59B6), 0);

    lbl_cpu_freq = lv_label_create(cont);
    lv_obj_set_style_text_font(lbl_cpu_freq, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(lbl_cpu_freq, lv_color_hex(0xE67E22), 0);

    lbl_freq_dist = lv_label_create(cont);
    lv_obj_set_style_text_font(lbl_freq_dist, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl_freq_dist, lv_color_hex(0x95A5A6), 0);

    lbl_temp = lv_label_create(cont);
    lv_obj_set_style_text_font(lbl_temp, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(lbl_temp, lv_color_hex(0x1ABC9C), 0);

    lbl_heap = lv_label_create(cont);
    lv_obj_set_style_text_font(lbl_heap, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(lbl_heap, lv_color_hex(0x2ECC71), 0);

    lbl_psram = lv_label_create(cont);
    lv_obj_set_style_text_font(lbl_psram, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(lbl_psram, lv_color_hex(0xF1C40F), 0);

    lbl_uptime = lv_label_create(cont);
    lv_obj_set_style_text_font(lbl_uptime, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(lbl_uptime, lv_color_hex(0xE74C3C), 0);

    update_timer = lv_timer_create(update_stats, 1000, NULL);
    lv_timer_pause(update_timer);
}

void ui_sys_stats_set_active(bool active) {
    if (active) {
        lv_timer_resume(update_timer);
        reset_cpu_freq_distribution();
        update_stats(update_timer);
    } else {
        lv_timer_pause(update_timer);
    }
}
