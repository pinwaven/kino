#include "ui_app.h"

#define BLE_UI_FONT (&lv_font_source_han_sans_sc_16_cjk)

void ui_ble_set_active(bool active)
{
    (void)active;
}

void ui_ble_init(lv_obj_t *tile)
{
    lv_obj_set_style_bg_color(tile, lv_color_hex(0x05070A), 0);
    lv_obj_set_style_opa(tile, LV_OPA_60, 0);
    lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(tile);
    lv_label_set_text(title, "BT控制");
    lv_obj_set_style_text_font(title, BLE_UI_FONT, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x8A8F98), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 54);

    lv_obj_t *name = lv_label_create(tile);
    lv_label_set_text(name, "KINO_CTRL");
    lv_obj_set_style_text_font(name, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(name, lv_color_hex(0x6F7782), 0);
    lv_obj_align(name, LV_ALIGN_TOP_MID, 0, 92);

    lv_obj_t *state_label = lv_label_create(tile);
    lv_obj_set_width(state_label, 320);
    lv_obj_set_style_text_align(state_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(state_label, BLE_UI_FONT, 0);
    lv_obj_set_style_text_color(state_label, lv_color_hex(0x9AA3AD), 0);
    lv_obj_align(state_label, LV_ALIGN_CENTER, 0, -28);
    lv_label_set_text(state_label, "BT: 已禁用");

    lv_obj_t *start_btn = lv_button_create(tile);
    lv_obj_set_size(start_btn, 120, 46);
    lv_obj_align(start_btn, LV_ALIGN_CENTER, -70, 30);
    lv_obj_set_style_bg_color(start_btn, lv_color_hex(0x4B5563), 0);
    lv_obj_set_style_radius(start_btn, 23, 0);
    lv_obj_add_state(start_btn, LV_STATE_DISABLED);

    lv_obj_t *start_label = lv_label_create(start_btn);
    lv_label_set_text(start_label, "起动");
    lv_obj_set_style_text_font(start_label, BLE_UI_FONT, 0);
    lv_obj_center(start_label);

    lv_obj_t *stop_btn = lv_button_create(tile);
    lv_obj_set_size(stop_btn, 120, 46);
    lv_obj_align(stop_btn, LV_ALIGN_CENTER, 70, 30);
    lv_obj_set_style_bg_color(stop_btn, lv_color_hex(0x4B5563), 0);
    lv_obj_set_style_radius(stop_btn, 23, 0);
    lv_obj_add_state(stop_btn, LV_STATE_DISABLED);

    lv_obj_t *stop_label = lv_label_create(stop_btn);
    lv_label_set_text(stop_label, "停止");
    lv_obj_set_style_text_font(stop_label, BLE_UI_FONT, 0);
    lv_obj_center(stop_label);

    lv_obj_t *svc = lv_label_create(tile);
    lv_label_set_text(svc, "服務 FFE0  寫入 FFE1  通知 FFE2");
    lv_obj_set_width(svc, 360);
    lv_obj_set_style_text_align(svc, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(svc, BLE_UI_FONT, 0);
    lv_obj_set_style_text_color(svc, lv_color_hex(0x666D76), 0);
    lv_obj_align(svc, LV_ALIGN_CENTER, 0, 84);

    lv_obj_t *result_label = lv_label_create(tile);
    lv_obj_set_width(result_label, 340);
    lv_obj_set_style_text_align(result_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(result_label, BLE_UI_FONT, 0);
    lv_obj_set_style_text_color(result_label, lv_color_hex(0x8A8F98), 0);
    lv_label_set_long_mode(result_label, LV_LABEL_LONG_WRAP);
    lv_obj_align(result_label, LV_ALIGN_BOTTOM_MID, 0, -70);
    lv_label_set_text(result_label, "命令: --\nBT: 已禁用");

    lv_obj_t *commands = lv_label_create(tile);
    lv_label_set_text(commands, "指令 open close homing stop nfc status hi");
    lv_obj_set_width(commands, 360);
    lv_obj_set_style_text_align(commands, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(commands, BLE_UI_FONT, 0);
    lv_obj_set_style_text_color(commands, lv_color_hex(0x666D76), 0);
    lv_obj_align(commands, LV_ALIGN_BOTTOM_MID, 0, -38);
}
