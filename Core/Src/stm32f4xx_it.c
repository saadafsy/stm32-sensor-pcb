/* stm32f4xx_it.c — CPU exception handlers.
 * SVC/PendSV/SysTick are owned by FreeRTOS (mapped in FreeRTOSConfig.h), so they are
 * deliberately NOT defined here — exactly one definition of each must exist. */
#include "stm32f4xx.h"

void NMI_Handler(void)        { while (1) { } }
void HardFault_Handler(void)  { while (1) { } }   /* set a breakpoint here to catch faults */
void MemManage_Handler(void)  { while (1) { } }
void BusFault_Handler(void)   { while (1) { } }
void UsageFault_Handler(void) { while (1) { } }
