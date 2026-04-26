#include "lvgl.h"
#include "animated_dot_svg.h"

/* Animated Dot SVG data */
const char animated_dot_svg_data[] = 
"<svg width=\"200\" height=\"200\" viewBox=\"0 0 200 200\" xmlns=\"http://www.w3.org/2000/svg\">"
"<circle cx=\"100\" cy=\"100\" r=\"80\" fill=\"none\" stroke=\"#333\" stroke-width=\"1\" stroke-dasharray=\"4 4\" />"
"<circle r=\"10\" fill=\"#00FF00\">"
"<animateMotion dur=\"3s\" repeatCount=\"indefinite\" path=\"M 100,100 m -80,0 a 80,80 0 1,0 160,0 a 80,80 0 1,0 -160,0\" />"
"</circle></svg>";

const lv_image_dsc_t animated_dot_svg_dsc = {
  .header = {
    .magic = LV_IMAGE_HEADER_MAGIC,
    .cf = LV_COLOR_FORMAT_RAW,
    .w = 200,
    .h = 200,
  },
  .data_size = sizeof(animated_dot_svg_data),
  .data = (const uint8_t *)animated_dot_svg_data,
};
