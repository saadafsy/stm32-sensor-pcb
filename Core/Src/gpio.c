/* gpio.c — onboard LED is on PA5 (LD2 on the Nucleo; a dedicated pin on the PCB). */
#include "stm32f4xx.h"
#include "gpio.h"

void led_init(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;          /* clock the port first, always */
    GPIOA->MODER &= ~(3U << (5 * 2));             /* clear the 2 MODER bits for pin 5 */
    GPIOA->MODER |=  (1U << (5 * 2));             /* 01 = general-purpose output    */
}

void led_toggle(void) { GPIOA->ODR ^= (1U << 5); }  /* flip just PA5 in the output reg */
