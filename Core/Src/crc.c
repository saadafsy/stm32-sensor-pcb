/* crc.c — CRC-16/CCITT (poly 0x1021, init 0xFFFF, no final XOR).
 * A CRC is a checksum with good error-detection: the receiver recomputes it over
 * the same bytes and compares. A mismatch means the bytes were corrupted in transit. */
#include "crc.h"

uint16_t crc16_ccitt(const uint8_t *data, uint32_t len)
{
    uint16_t crc = 0xFFFF;                  /* standard CCITT seed */
    for (uint32_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;      /* XOR byte into the high 8 bits */
        for (int b = 0; b < 8; b++) {       /* process 8 bits, MSB first */
            if (crc & 0x8000) crc = (uint16_t)((crc << 1) ^ 0x1021);
            else              crc = (uint16_t)(crc << 1);
        }
    }
    return crc;
}
