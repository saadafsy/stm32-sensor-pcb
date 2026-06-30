/* protocol.h */
#ifndef PROTOCOL_H
#define PROTOCOL_H
#include <stdint.h>
#define FRAME_START        0xAAu
#define FRAME_END          0x55u
#define FRAME_PAYLOAD_LEN  16u
#define FRAME_TOTAL_LEN    21u
/* Pack one telemetry record into out[] (must be >= FRAME_TOTAL_LEN). Returns bytes written. */
uint32_t protocol_build(uint8_t *out, uint32_t seq,
                        int32_t temp_c100, uint32_t press_pa, uint32_t hum_c100);
#endif
