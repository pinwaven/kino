# Project Kino: Waveshare ESP32-S3 AMOLED 1.75"

This project is a standalone implementation for the **Waveshare ESP32-S3-Touch-AMOLED-1.75** development board. It features a centered SVG logo, interactive UI elements, and a random color-changing ring.

## Hardware Specifications
- **MCU:** ESP32-S3-WROOM-1-N16R8 (16MB Flash, 8MB OPI PSRAM)
- **Display:** 1.75" AMOLED (466x466), CO5300 driver via QSPI
- **Touch:** CST92xx via I2C
- **Sensors:** QMI8658 6-axis IMU

## Project Status
- [x] Hardware connectivity verified.
- [x] Standalone library structure implemented.
- [x] LVGL v9.4.0 integration successful.
- [x] Native SVG rendering functional.
- [x] Touch interactivity functional.

## Critical Technical Resolutions

### 1. GFX Library Compatibility (ESP32 Core v3.x)
**Issue:** `GFX_Library_for_Arduino` failed to compile with ESP32 Arduino Core v3.0.x.
**Fix:** Patched `Arduino_ESP32QSPI.cpp` with a fallback definition for `ESP_INTR_CPU_AFFINITY_AUTO`.

### 2. LVGL v9 Upgrade & Rendering
**Issue:** Transitioning to v9 requires a new display and input device API.
**Fix:** 
- Replaced `lv_disp_drv_t` with `lv_display_create()`.
- Replaced `lv_indev_drv_t` with `lv_indev_create()`.
- **Rings/Segments Fix:** Implemented the rounder callback via the new event system to ensure 2-pixel alignment for AMOLED hardware:
  ```cpp
  lv_display_add_event_cb(disp, rounder_event_cb, LV_EVENT_INVALIDATE_AREA, NULL);
  ```

### 3. SVG Rendering Solution (CRITICAL)
**Issue:** Passing a raw SVG string to `lv_image_set_src()` often fails because the decoder cannot reliably identify the source type.
**Fix:** Wrap the SVG string data in an `lv_image_dsc_t` structure. This forces the decoder to recognize the data size and format:
```cpp
const lv_image_dsc_t waven_logo_svg_dsc = {
  .header = {
    .magic = LV_IMAGE_HEADER_MAGIC,
    .cf = LV_COLOR_FORMAT_RAW, // Important for SVG
    .w = 150, // Nominal width
    .h = 105, // Nominal height
  },
  .data_size = (uint32_t)sizeof("<svg ... </svg>"),
  .data = (const uint8_t *)waven_logo_svg_data,
};
```

### 4. Memory Management (PSRAM)
**Issue:** ThorVG (SVG engine) and LVGL v9 require significant heap memory for vector rasterization.
**Fix:** Configured `lv_conf.h` to use the system `malloc` (`LV_STDLIB_CLIB`), allowing LVGL to automatically utilize the 8MB of OPI PSRAM available on the ESP32-S3.

## Project Structure
```text
kino/
├── kino.ino                # Main orchestrator
├── pin_config.h            # Hardware pin definitions
├── src/
│   ├── hal/                # Hardware Abstraction Layer (Display, Touch)
│   ├── ui/                 # UI layouts, screens, and assets
│   │   └── assets/         # SVG/Image sources and descriptors
│   └── core/               # Biomarker analysis logic
├── libraries/              # Local dependencies (LVGL v9, GFX, etc.)
└── docs/                   # Documentation and Changelog
```

## Build & Upload Instructions
This project is standalone.

### 1. Compile
```bash
arduino-cli compile --fqbn esp32:esp32:esp32s3 --libraries libraries kino.ino
```

### 2. Upload
Replace `/dev/cu.usbmodem14201` with your actual device port.
```bash
arduino-cli upload -p /dev/cu.usbmodem14201 --fqbn esp32:esp32:esp32s3 kino.ino
```

**Required Board Settings:**
- **Flash Size:** 16MB
- **PSRAM:** OPI PSRAM
- **Flash Mode:** DIO
- **Core Version:** ESP32 Core v3.0.x
