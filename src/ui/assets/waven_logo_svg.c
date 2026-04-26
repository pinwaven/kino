#include "lvgl.h"
#include "waven_logo_svg.h"

/* Waven Logo SVG data */
const char waven_logo_svg_data[] = 
"<svg viewBox=\"78 376 103 73\" xmlns=\"http://www.w3.org/2000/svg\">"
"<path d=\"M85.3 411.4h21L108.3 399.3h-21z\" fill=\"#257abf\"/>"
"<path d=\"M78.1 392.8h31l2.8-16.1h-31z\" fill=\"#257abf\"/>"
"<path d=\"M178.4 376.7h-13.4c-1.2 0-2.3 1.1-2.5 2.5l-6.2 38.4c-1.8 11.5-6.2 13.3-12 13.3-2.3 0-4.4-.1-6.3-.8l8.2-50.7v-.5c0-1.1-.6-2.1-1.8-2.1h-13.8c-1.2 0-2.4 1.2-2.7 2.6l-8 50.7c-2.1.7-4.1.9-6.5.9-5.1 0-8.3-1-8.3-7.6 0-1.2.1-2.8.4-4.5h-18.1c-.3 2-.4 4-.4 5.9 0 16.3 10.4 24.1 21.9 24.1 6.3 0 11.6-.7 17.8-4.2 5.1 3.5 11.1 4.2 17.4 4.2 15.9 0 26.5-7.5 30.3-31.1l6.2-38.4v-.4c0-1.2-.9-2.4-2-2.4\" fill=\"#257abf\"/>"
"</svg>";

const lv_image_dsc_t waven_logo_svg_dsc = {
  .header = {
    .magic = LV_IMAGE_HEADER_MAGIC,
    .cf = LV_COLOR_FORMAT_RAW,
    .w = 150,
    .h = 105,
  },
  .data_size = sizeof(waven_logo_svg_data),
  .data = (const uint8_t *)waven_logo_svg_data,
};
