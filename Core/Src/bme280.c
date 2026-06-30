/* bme280.c — BME280 over I2C. Normal mode, oversampling x1, IIR filter off. */
#include "stm32f4xx.h"
#include "i2c.h"
#include "bme280.h"

#define BME280_ADDR     0x77      /* Adafruit breakout default (SDO high/float).
                                     Use 0x76 if you tie SDO to GND. */
#define REG_ID          0xD0      /* reads 0x60 for a real BME280 (0x58 = BMP280!) */
#define REG_RESET       0xE0      /* write 0xB6 to soft-reset */
#define REG_CTRL_HUM    0xF2
#define REG_CTRL_MEAS   0xF4
#define REG_CONFIG      0xF5
#define REG_DATA        0xF7      /* press(3) temp(3) hum(2) = 8 bytes, big-endian-ish */
#define REG_CALIB00     0x88      /* T1..T3, P1..P9, then a gap          (26 bytes) */
#define REG_CALIB26     0xE1      /* H2..H6                               (7 bytes)  */

/* Per-chip calibration, populated by bme280_read_calib(). Signedness matters —
 * get one of these wrong and the math is subtly off, not obviously broken. */
static uint16_t T1, P1; static int16_t T2,T3,P2,P3,P4,P5,P6,P7,P8,P9;
static uint8_t  H1, H3; static int16_t H2; static int16_t H4, H5; static int8_t H6;

static int32_t t_fine;    /* carries fine temperature from T into P and H */

static void bme280_read_calib(void)
{
    uint8_t c[26];
    i2c_read_regs(BME280_ADDR, REG_CALIB00, c, 26);     /* 0x88..0xA1 */
    T1=(uint16_t)(c[0]|(c[1]<<8));  T2=(int16_t)(c[2]|(c[3]<<8));  T3=(int16_t)(c[4]|(c[5]<<8));
    P1=(uint16_t)(c[6]|(c[7]<<8));  P2=(int16_t)(c[8]|(c[9]<<8));  P3=(int16_t)(c[10]|(c[11]<<8));
    P4=(int16_t)(c[12]|(c[13]<<8)); P5=(int16_t)(c[14]|(c[15]<<8));P6=(int16_t)(c[16]|(c[17]<<8));
    P7=(int16_t)(c[18]|(c[19]<<8)); P8=(int16_t)(c[20]|(c[21]<<8));P9=(int16_t)(c[22]|(c[23]<<8));
    H1=c[25];                                            /* 0xA1 */

    uint8_t h[7];
    i2c_read_regs(BME280_ADDR, REG_CALIB26, h, 7);       /* 0xE1..0xE7 */
    H2=(int16_t)(h[0]|(h[1]<<8));
    H3=h[2];
    H4=(int16_t)((h[3]<<4) | (h[4] & 0x0F));             /* H4 is 12-bit, split oddly */
    H5=(int16_t)((h[5]<<4) | (h[4] >> 4));               /* H5 shares byte h[4] with H4 */
    H6=(int8_t)h[6];
}

int bme280_init(void)
{
    uint8_t id = 0;
    /* If the bus is dead or nobody ACKs the address, i2c_read_regs returns < 0.
     * Distinguish that (-2: no device on the bus) from a wrong-but-present chip (-1). */
    if (i2c_read_regs(BME280_ADDR, REG_ID, &id, 1) != 0) return -2;   /* NACK / stuck bus */
    if (id != 0x60) return -1;                            /* 0x58 => BMP280; 0xFF => wiring */

    i2c_write_reg(BME280_ADDR, REG_RESET, 0xB6);          /* soft reset */
    for (volatile int d=0; d<100000; d++) { }             /* brief settle (>2 ms) */

    bme280_read_calib();

    /* ctrl_hum must be written BEFORE ctrl_meas (it only latches on a ctrl_meas write). */
    i2c_write_reg(BME280_ADDR, REG_CTRL_HUM, 0x01);       /* humidity oversampling x1 */
    /* ctrl_meas: osrs_t=001(x1) osrs_p=001(x1) mode=11(normal) -> 0b001_001_11 = 0x27 */
    i2c_write_reg(BME280_ADDR, REG_CTRL_MEAS, 0x27);
    /* config: t_sb=000(0.5ms) filter=000(off) spi3w=0 -> 0x00 (fast free-running) */
    i2c_write_reg(BME280_ADDR, REG_CONFIG, 0x00);
    return 0;
}

/* ---- Bosch fixed-point compensation (verbatim from the BME280 datasheet) ---- */

/* Temperature: returns DegC * 100  (e.g. 2345 = 23.45 C). Also sets t_fine. */
static int32_t compensate_T(int32_t adc_T)
{
    int32_t v1 = ((((adc_T>>3) - ((int32_t)T1<<1))) * ((int32_t)T2)) >> 11;
    int32_t v2 = (((((adc_T>>4) - ((int32_t)T1)) * ((adc_T>>4) - ((int32_t)T1))) >> 12)
                 * ((int32_t)T3)) >> 14;
    t_fine = v1 + v2;
    return (t_fine * 5 + 128) >> 8;
}

/* Pressure: 64-bit math, returns Pa in Q24.8 (value>>8 = Pa). 64-bit gives full accuracy. */
static uint32_t compensate_P(int32_t adc_P)
{
    int64_t v1, v2, p;
    v1 = ((int64_t)t_fine) - 128000;
    v2 = v1 * v1 * (int64_t)P6;
    v2 = v2 + ((v1 * (int64_t)P5) << 17);
    v2 = v2 + (((int64_t)P4) << 35);
    v1 = ((v1 * v1 * (int64_t)P3) >> 8) + ((v1 * (int64_t)P2) << 12);
    v1 = (((((int64_t)1) << 47) + v1)) * ((int64_t)P1) >> 33;
    if (v1 == 0) return 0;                                /* guard div-by-zero */
    p  = 1048576 - adc_P;
    p  = (((p << 31) - v2) * 3125) / v1;
    v1 = (((int64_t)P9) * (p >> 13) * (p >> 13)) >> 25;
    v2 = (((int64_t)P8) * p) >> 19;
    p  = ((p + v1 + v2) >> 8) + (((int64_t)P7) << 4);
    return (uint32_t)p;
}

/* Humidity: returns %RH in Q22.10 (value>>10 = %RH). */
static uint32_t compensate_H(int32_t adc_H)
{
    int32_t v = t_fine - (int32_t)76800;
    v = (((((adc_H << 14) - (((int32_t)H4) << 20) - (((int32_t)H5) * v)) + 16384) >> 15)
        * (((((((v * (int32_t)H6) >> 10) * (((v * (int32_t)H3) >> 11) + 32768)) >> 10)
        + 2097152) * ((int32_t)H2) + 8192) >> 14));
    v = v - ((((( v >> 15) * (v >> 15)) >> 7) * ((int32_t)H1)) >> 4);
    if (v < 0) v = 0;
    if (v > 419430400) v = 419430400;
    return (uint32_t)(v >> 12);
}

/* Read one fresh sample and compensate. Outputs: T in 0.01C, P in Q24.8 Pa, H in Q22.10 %RH. */
int bme280_read(int32_t *T_out, uint32_t *P_out, uint32_t *H_out)
{
    uint8_t d[8];
    i2c_read_regs(BME280_ADDR, REG_DATA, d, 8);           /* N=8 burst read */

    /* Re-assemble the packed ADC words. Pressure & temperature are 20-bit
     * (msb<<12 | lsb<<4 | xlsb>>4); humidity is 16-bit (msb<<8 | lsb). */
    int32_t adc_P = ((int32_t)d[0] << 12) | ((int32_t)d[1] << 4) | (d[2] >> 4);
    int32_t adc_T = ((int32_t)d[3] << 12) | ((int32_t)d[4] << 4) | (d[5] >> 4);
    int32_t adc_H = ((int32_t)d[6] << 8)  |  (int32_t)d[7];

    *T_out = compensate_T(adc_T);     /* MUST run first — it sets t_fine */
    *P_out = compensate_P(adc_P);
    *H_out = compensate_H(adc_H);
    return 0;
}
