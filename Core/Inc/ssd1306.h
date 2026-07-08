/* ssd1306.h */
#ifndef SSD1306_H
#define SSD1306_H
#include <stdint.h>
#define SSD1306_ADDR 0x3C /* 7-bit address (some modules are 0x3D) */
void ssd1306_init(void); /* run once; uses I2C (charge pump on, etc.) */
void ssd1306_clear(void); /* clear the RAM framebuffer (no I2C) */
void ssd1306_draw_string(uint8_t col, uint8_t page, const char *s); /* into RAM */
void ssd1306_flush(void); /* push the whole framebuffer over I2C */
#endif
