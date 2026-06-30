/* uart.c — USART2, 921600 8-N-1, blocking TX. PCLK1 = 50 MHz. */
#include "stm32f4xx.h"
#include "uart.h"

#define UART_TX_TIMEOUT 1000000U   /* a healthy 921600 byte clears TXE in ~11 us; this is
                                      a safety net so a mis-clocked/disabled USART can't hang */

void usart2_init(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;          /* GPIOA clock  */
    RCC->APB1ENR |= RCC_APB1ENR_USART2EN;         /* USART2 clock */

    /* PA2 = USART2_TX, PA3 = USART2_RX, both in alternate-function mode (10),
     * alternate function AF7 (the USART2 function). */
    GPIOA->MODER &= ~((3U << (2 * 2)) | (3U << (3 * 2)));
    GPIOA->MODER |=  ((2U << (2 * 2)) | (2U << (3 * 2)));
    GPIOA->AFR[0] &= ~((0xFU << (2 * 4)) | (0xFU << (3 * 4)));
    GPIOA->AFR[0] |=  ((7U   << (2 * 4)) | (7U   << (3 * 4)));   /* AF7 */

    /* Baud: BRR holds USARTDIV in 12.4 fixed point. For 921600 @ 50 MHz with
     * 16x oversampling, USARTDIV = 50e6/(16*921600) = 3.3908, *16 = 54.25 -> 54.
     * Real baud = 50e6/54 = 925926 (+0.47%), well within UART tolerance. */
    USART2->BRR = 54U;

    USART2->CR1 = USART_CR1_TE | USART_CR1_RE;    /* enable transmitter + receiver */
    USART2->CR1 |= USART_CR1_UE;                  /* enable the USART itself        */
}

void uart_putc(char c)
{
    uint32_t to = UART_TX_TIMEOUT;
    while (!(USART2->SR & USART_SR_TXE)) {        /* wait until the data reg is empty */
        if (--to == 0u) return;                  /* TXE never came -> bail (don't hang) */
    }
    USART2->DR = (uint8_t)c;                      /* writing DR launches the byte     */
}

void uart_write(const char *s)
{
    uint32_t to;
    while (*s) uart_putc(*s++);
    to = UART_TX_TIMEOUT;
    while (!(USART2->SR & USART_SR_TC)) {         /* wait for the last byte to finish
                                                     before returning — matters when a
                                                     STOP/sleep could truncate the line */
        if (--to == 0u) return;
    }
}
