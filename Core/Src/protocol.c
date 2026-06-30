/* protocol.c */
#include "protocol.h"
#include "crc.h"

/* Write a 32-bit value little-endian (low byte first). The host must read it the
 * same way — endianness mismatch is the #1 binary-protocol bug. */
static void put_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v      );
    p[1] = (uint8_t)(v >>  8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

uint32_t protocol_build(uint8_t *out, uint32_t seq,
                        int32_t temp_c100, uint32_t press_pa, uint32_t hum_c100)
{
    out[0] = FRAME_START;
    out[1] = (uint8_t)FRAME_PAYLOAD_LEN;          /* 16 */
    put_u32(&out[2],  seq);
    put_u32(&out[6],  (uint32_t)temp_c100);       /* reinterpret the signed bits as-is */
    put_u32(&out[10], press_pa);
    put_u32(&out[14], hum_c100);

    uint16_t crc = crc16_ccitt(&out[1], 1u + FRAME_PAYLOAD_LEN);  /* over LEN + payload = 17 bytes */
    out[18] = (uint8_t)(crc      );               /* CRC low byte  */
    out[19] = (uint8_t)(crc >>  8);               /* CRC high byte */
    out[20] = FRAME_END;
    return FRAME_TOTAL_LEN;                        /* 21 */
}
