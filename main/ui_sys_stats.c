#include "ui_app.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "esp_flash.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>

static lv_obj_t *lbl_heap;
static lv_obj_t *lbl_psram;
static lv_obj_t *lbl_uptime;
static lv_obj_t *lbl_flash;
static lv_obj_t *lbl_cpu_core0;
static lv_obj_t *lbl_cpu_core1;
static lv_obj_t *spinner; 
static lv_timer_t *update_timer;

static uint32_t prev_idle_ticks[2] = {0, 0};
static uint32_t prev_total_ticks = 0;

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
    lv_obj_set_style_pad_gap(cont, 8, 0);
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);

    // Title
    lv_obj_t *title = lv_label_create(cont);
    lv_label_set_text(title, "SYSTEM MONITOR");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x3498DB), 0);
    lv_obj_set_style_margin_bottom(title, 10, 0);

    // Spinner for load visualization
    spinner = lv_spinner_create(cont);
    lv_obj_set_size(spinner, 40, 40);
    lv_obj_set_style_arc_width(spinner, 5, LV_PART_MAIN);
    lv_obj_set_style_arc_width(spinner, 5, LV_PART_INDICATOR);
    lv_obj_set_style_margin_bottom(spinner, 10, 0);

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
        lv_obj_remove_flag(spinner, LV_OBJ_FLAG_HIDDEN);
        update_stats(update_timer);
    } else {
        lv_timer_pause(update_timer);
        lv_obj_add_flag(spinner, LV_OBJ_FLAG_HIDDEN);
    }
}
