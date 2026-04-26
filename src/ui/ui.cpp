#include "ui.h"
#include "lvgl.h"
#include "assets/waven_logo_svg.h"
#include <stdlib.h>

static lv_obj_t * text_circle;

void ui_init() {
  lv_obj_t * screen = lv_screen_active();
  lv_obj_set_style_bg_color(screen, lv_color_hex(0x000000), 0);

  // Circles
  lv_obj_t * edge = lv_obj_create(screen);
  lv_obj_set_size(edge, 460, 460); lv_obj_center(edge);
  lv_obj_set_style_radius(edge, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_opa(edge, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_color(edge, lv_color_hex(0x0000FF), 0);
  lv_obj_set_style_border_width(edge, 3, 0);

  text_circle = lv_obj_create(screen);
  lv_obj_set_size(text_circle, 320, 320); lv_obj_center(text_circle);
  lv_obj_set_style_radius(text_circle, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_opa(text_circle, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_color(text_circle, lv_color_hex(0x00FF00), 0);
  lv_obj_set_style_border_width(text_circle, 5, 0);

  // Layout Container
  lv_obj_t * cont = lv_obj_create(screen);
  lv_obj_set_size(cont, 300, 150); lv_obj_center(cont);
  lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(cont, 0, 0);
  lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  // SVG IMAGE - Using Descriptor from assets
  lv_obj_t * svg_img = lv_image_create(cont);
  lv_image_set_src(svg_img, &waven_logo_svg_dsc);
  
  // BUTTON
  lv_obj_t * btn = lv_button_create(cont);
  lv_obj_set_size(btn, 80, 50);
  lv_obj_t * bl = lv_label_create(btn); lv_label_set_text(bl, "CLR"); lv_obj_center(bl);
  lv_obj_add_event_cb(btn, [](lv_event_t*e){ 
    lv_obj_set_style_border_color(text_circle, lv_color_hex(rand() % 0xFFFFFF), 0);
  }, LV_EVENT_CLICKED, NULL);

  lv_obj_t * debug_label = lv_label_create(screen);
  lv_label_set_text(debug_label, "Kino Biomarker Analyzer");
  lv_obj_align(debug_label, LV_ALIGN_BOTTOM_MID, 0, -40);
}
