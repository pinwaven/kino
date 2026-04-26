#include "lvgl.h"
#include "animated_dot_svg.h"

/* Simplified Animated Dot SVG data with initial positions */
const char animated_dot_svg_data[] = 
"<svg width=\"200\" height=\"200\" viewBox=\"0 0 200 200\" xmlns=\"http://www.w3.org/2000/svg\">"
"<circle cx=\"100\" cy=\"100\" r=\"80\" fill=\"none\" stroke=\"#444\" stroke-width=\"2\" />"
"<circle cx=\"100\" cy=\"100\" r=\"20\" fill=\"#FF0000\">"
"<animate attributeName=\"cx\" values=\"40;160;40\" dur=\"2s\" repeatCount=\"indefinite\" />"
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
