/* uart.h */
#ifndef UART_H
#define UART_H

void usart2_init(void);          /* USART2, 921600 8-N-1, blocking TX */
void uart_putc(char c);          /* send one byte (length-safe; a byte may be 0x00) */
void uart_write(const char *s);  /* send a C string, then wait for line to drain    */

#endif /* UART_H */
