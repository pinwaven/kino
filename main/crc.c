#include "crc.h"

static uint8_t bit_reverse(uint8_t b) {
    b = (b & 0xF0) >> 4 | (b & 0x0F) << 4;
    b = (b & 0xCC) >> 2 | (b & 0x33) << 2;
    b = (b & 0xAA) >> 1 | (b & 0x55) << 1;
    return b;
}

uint8_t cal_crc8(const uint8_t *data, uint16_t len) {
    uint8_t crc = 0x00;
    for (uint16_t i = 0; i < len; i++) {
        // refin=True: 输入字节位反转
        uint8_t byte = bit_reverse(data[i]);
        crc ^= byte;
        for (int j = 0; j < 8; j++) {
            if (crc & 0x80) {
                crc = (crc << 1) ^ 0x07; // Poly=0x07
            } else {
                crc <<= 1;
            }
        }
    }
    // refout=False: 结果不反转
    return crc;
}
