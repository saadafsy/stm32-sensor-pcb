#include "stm32f4xx.h"        // CMSIS device header: defines RCC, FLASH, GPIOx, I2C1, USART2 ...
#include "clock.h"

void clock_init(void)
{
    /* 1) Flash access latency MUST be raised before increasing the clock.
     *    At 100 MHz / 3.3 V the flash needs 3 wait states (per the F411 datasheet).
     *    We also turn on the prefetch buffer and the I/D caches to recover the
     *    performance the wait states cost. Do this FIRST — if the CPU speeds up
     *    while flash is still at 0 WS, the very next instruction fetch returns
     *    garbage and you hard-fault instantly. */
    FLASH->ACR = FLASH_ACR_LATENCY_3WS | FLASH_ACR_PRFTEN
               | FLASH_ACR_ICEN | FLASH_ACR_DCEN;

    /* 2) Start the High-Speed External oscillator (8 MHz) and wait until the
     *    hardware reports it is stable. Skipping the wait = locking the PLL to
     *    a not-yet-stable reference = intermittent boot failures. */
    RCC->CR |= RCC_CR_HSEON;
    while (!(RCC->CR & RCC_CR_HSERDY)) { }

    /* 3) Configure the main PLL from HSE:  M=8, N=200, P=2  →  100 MHz.
     *      VCO_in  = 8 MHz / 8        = 1 MHz   (must be 1..2 MHz)  ✓
     *      VCO_out = 1 MHz * 200      = 200 MHz (must be 100..432) ✓
     *      SYSCLK  = 200 MHz / 2      = 100 MHz (<= 100 on F411)    ✓
     *    PLLP encodes /2 as the bit value 0. PLLQ is unused (no native USB),
     *    but we give it a legal value (4) so the field isn't reserved-zero. */
    RCC->PLLCFGR = (8U   << RCC_PLLCFGR_PLLM_Pos)
                 | (200U << RCC_PLLCFGR_PLLN_Pos)
                 | (0U   << RCC_PLLCFGR_PLLP_Pos)     /* /2 */
                 | RCC_PLLCFGR_PLLSRC_HSE             /* HSE as PLL input */
                 | (4U   << RCC_PLLCFGR_PLLQ_Pos);

    /* 4) Enable the PLL and wait for it to lock. */
    RCC->CR |= RCC_CR_PLLON;
    while (!(RCC->CR & RCC_CR_PLLRDY)) { }

    /* 5) Set bus prescalers BEFORE switching SYSCLK, so no bus is ever over-clocked:
     *      AHB  /1  -> HCLK  = 100 MHz
     *      APB1 /2  -> PCLK1 =  50 MHz  (I2C1 & USART2 live here — APB1 max is 50)
     *      APB2 /1  -> PCLK2 = 100 MHz */
    RCC->CFGR &= ~(RCC_CFGR_HPRE | RCC_CFGR_PPRE1 | RCC_CFGR_PPRE2);
    RCC->CFGR |= RCC_CFGR_HPRE_DIV1 | RCC_CFGR_PPRE1_DIV2 | RCC_CFGR_PPRE2_DIV1;

    /* 6) Switch the system clock source to the PLL and wait for hardware to
     *    confirm the switch (SWS reflects the *actual* active source). */
    RCC->CFGR |= RCC_CFGR_SW_PLL;
    while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL) { }

    /* 7) Keep the CMSIS global in sync (FreeRTOS/printf timing may read it). */
    SystemCoreClock = 100000000UL;
}
