/* iwdg.c -- call iwdg_init() once after the clock is up. ~1.6 s nominal timeout. */
#include "stm32f4xx.h"
void iwdg_init(void)
{
 IWDG->KR = 0x5555; /* unlock PR/RLR */
 IWDG->PR = 0x06; /* prescaler /256 -> 32 kHz/256 ~= 125 Hz */
 IWDG->RLR = 200; /* reload: 200/125 ~= 1.6 s timeout */
 while (IWDG->SR) { } /* wait for PR/RLR writes to commit */
 IWDG->KR = 0xCCCC; /* start (cannot be stopped until reset) */
}
void iwdg_refresh(void) /* call from the heartbeat (lowest-priority) task */
{
 IWDG->KR = 0xAAAA; /* reload the counter */
}
