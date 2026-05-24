#ifndef CRC_H
#define CRC_H

#include <stdint.h>

#include <stdbool.h>

uint8_t cal_crc8(const uint8_t *data, uint16_t len);
bool crc_verify_equivalence(void);

#endif // CRC_H
