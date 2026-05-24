#ifndef CRC_H
#define CRC_H

#include <stdint.h>

#include <stdbool.h>

#define ENABLE_CRC_EQUIVALENCE_TEST 0

uint8_t cal_crc8(const uint8_t *data, uint16_t len);

#if ENABLE_CRC_EQUIVALENCE_TEST
bool crc_verify_equivalence(void);
#endif

#endif // CRC_H
