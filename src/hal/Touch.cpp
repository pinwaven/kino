#include "Touch.h"
#include "TouchDrvCSTXXX.hpp"
#include <Wire.h>
#include "../../pin_config.h"

static TouchDrvCST92xx touch;
static int16_t tx[5], ty[5];
static volatile bool isPressed = false;

static void my_touchpad_read(lv_indev_t * indev, lv_indev_data_t * data) {
  if (isPressed) {
    uint8_t touched = touch.getPoint(tx, ty, touch.getSupportTouchPoint());
    if (touched) {
      isPressed = false;
      data->state = LV_INDEV_STATE_PR;
      data->point.x = tx[0];
      data->point.y = ty[0];
    } else {
      data->state = LV_INDEV_STATE_REL;
    }
  } else {
    data->state = LV_INDEV_STATE_REL;
  }
}

void hal_touch_init() {
  Wire.begin(IIC_SDA, IIC_SCL);
  pinMode(TP_RESET, OUTPUT);
  digitalWrite(TP_RESET, LOW); delay(30);
  digitalWrite(TP_RESET, HIGH); delay(500); 
  
  if (touch.begin(Wire, 0x5A, IIC_SDA, IIC_SCL)) {
    touch.reset(); 
    touch.setMaxCoordinates(LCD_WIDTH, LCD_HEIGHT); 
    touch.setMirrorXY(true, true);
    pinMode(TP_INT, INPUT); 
    attachInterrupt(TP_INT, []() { isPressed = true; }, FALLING);
  }

  lv_indev_t * indev = lv_indev_create();
  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(indev, my_touchpad_read);
}
