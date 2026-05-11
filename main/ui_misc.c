#include "ui_app.h"
#include "stm32_interface.h"
#include "esp_log.h"
#include <string.h>

static void btn_event_cb(lv_event_t * e) {
    uintptr_t type = (uintptr_t)lv_event_get_user_data(e);

    static int toggle_state = 0;

    switch(type) {
        case 0: // Toggle Analog Power
            {
                toggle_state = !toggle_state;

                uint8_t payload[2];
                payload[0] = 0;
                payload[1] = (uint8_t)toggle_state;
                stm32_cmd_send_action(CMD_GPIO_WRITE, payload, 2);

                // set label text
                lv_obj_t *label = lv_obj_get_child(lv_event_get_target(e), 0);
                if (toggle_state) {
                    lv_label_set_text(label, "ANALOG PWR ON");
                    lv_obj_set_style_bg_color(lv_event_get_target(e), lv_color_hex(0xC0392B), 0);
                } else {
                    lv_label_set_text(label, "ANALOG PWR OFF");
                    lv_obj_set_style_bg_color(lv_event_get_target(e), lv_color_hex(0x27AE60), 0);
                }
            }
            break;
        case 1: // Hi
            {
                stm32_cmd_send_action(CMD_HI, NULL, 0);
            }
            break;
    }
}


void ui_misc_init(lv_obj_t *tile) {
    lv_obj_set_style_bg_color(tile, lv_color_hex(0x1f0f0f), 0);
    
    lv_obj_t *btn_cont = lv_obj_create(tile);
    lv_obj_set_size(btn_cont, 300, 260);
    lv_obj_set_style_bg_opa(btn_cont, 0, 0);
    lv_obj_set_style_border_width(btn_cont, 0, 0);
    lv_obj_align(btn_cont, LV_ALIGN_CENTER, 0, -20);
    lv_obj_set_flex_flow(btn_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(btn_cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(btn_cont, 10, 0);
    lv_obj_clear_flag(btn_cont, LV_OBJ_FLAG_SCROLLABLE);

    const char *btn_names[] = {"TOGGLE ANALOG PWR", "Hi"};
    lv_color_t btn_colors[] = {lv_color_hex(0x808080), lv_color_hex(0x9B59B6)};

    for(int i=0; i<2; i++) {
        lv_obj_t *btn = lv_button_create(btn_cont);
        lv_obj_set_size(btn, 180, 96);
        lv_obj_set_style_bg_color(btn, btn_colors[i], 0);
        lv_obj_set_style_radius(btn, 25, 0);
        
        lv_obj_t *l = lv_label_create(btn);
        lv_label_set_text(l, btn_names[i]);
        lv_obj_set_style_text_font(l, &lv_font_montserrat_14, 0);
        lv_obj_center(l);
        
        lv_obj_add_event_cb(btn, btn_event_cb, LV_EVENT_CLICKED, (void*)(uintptr_t)i);
    }
}
