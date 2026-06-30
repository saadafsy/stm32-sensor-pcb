/* bme280.h */
#ifndef BME280_H
#define BME280_H

#include <stdint.h>

/* Returns 0 on success, -1 wrong/absent chip ID, -2 no I2C ACK (bus/wiring). */
int bme280_init(void);

/* One fresh, compensated sample.
 *   T_out: temperature in 0.01 C   (e.g. 2345 = 23.45 C)
 *   P_out: pressure   in Q24.8 Pa  (value >> 8  = Pa)
 *   H_out: humidity   in Q22.10 %RH (value >> 10 = %RH) */
int bme280_read(int32_t *T_out, uint32_t *P_out, uint32_t *H_out);

#endif /* BME280_H */
