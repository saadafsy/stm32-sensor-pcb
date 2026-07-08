/* i2c.h */
#ifndef I2C_H
#define I2C_H

#include <stdint.h>

/* I2C1 master, fast mode 400 kHz, polled (no IRQ/DMA).
 * Transfer functions return: 0 = OK, -1 = timeout, -2 = address NACK. */
void i2c1_init(void);
int  i2c_write_reg(uint8_t addr7, uint8_t reg, uint8_t val);
int  i2c_read_regs(uint8_t addr7, uint8_t start_reg, uint8_t *buf, uint32_t n);


int i2c_write_buf(uint8_t addr7,uint8_t first,const uint8_t*buf,uint32_t n);
int i2c_write_reg(uint8_t addr7,uint8_t ctrl,uint8_t val);
void i2c_bus_recovery(void);
#endif /* I2C_H */
