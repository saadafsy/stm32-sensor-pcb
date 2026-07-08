/* i2c.c — I2C1 master, fast mode 400 kHz, PCLK1 = 50 MHz. Polled (no IRQ/DMA).
 * Return codes (transfer functions): 0 = OK, -1 = timeout (stuck bus / lost
 * clock-stretch), -2 = address NACK (no device answered). A production driver never
 * spins forever on a hardware flag: one wedged slave would hang the whole task. */
#include "stm32f4xx.h"
#include "i2c.h"

#define I2C_TIMEOUT  100000U   /* per-wait loop budget; ~a few ms at 100 MHz, >> any 400 kHz byte */

/* Spin until COND; give up after I2C_TIMEOUT tries -> force STOP, return -1.
 * (Using `return` inside a macro is a deliberate, documented bare-metal idiom here.) */
#define WAIT(cond) do {                                                   \
    uint32_t _to = I2C_TIMEOUT;                                           \
    while (!(cond)) {                                                      \
        if (--_to == 0u) { I2C1->CR1 |= I2C_CR1_STOP; return -1; }         \
    }                                                                      \
} while (0)

/* Wait for the bus to be idle before a transfer. */
#define WAIT_IDLE() do {                                                  \
    uint32_t _to = I2C_TIMEOUT;                                           \
    while (I2C1->SR2 & I2C_SR2_BUSY) { if (--_to == 0u) return -1; }       \
} while (0)

/* Wait for ADDR, but bail out if the slave NACKs the address (AF set = nobody home). */
#define WAIT_ADDR() do {                                                  \
    uint32_t _to = I2C_TIMEOUT;                                           \
    while (!(I2C1->SR1 & I2C_SR1_ADDR)) {                                  \
        if (I2C1->SR1 & I2C_SR1_AF) {                                      \
            I2C1->SR1 &= ~I2C_SR1_AF;       /* clear the NACK flag */      \
            I2C1->CR1 |= I2C_CR1_STOP;      /* release the bus    */       \
            return -2;                                                     \
        }                                                                  \
        if (--_to == 0u) { I2C1->CR1 |= I2C_CR1_STOP; return -1; }         \
    }                                                                      \
} while (0)

void i2c1_init(void)
{
    /* --- clocks --- */
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOBEN;          /* port B clock */
    RCC->APB1ENR |= RCC_APB1ENR_I2C1EN;           /* I2C1 clock   */

    /* --- PB8 = SCL, PB9 = SDA: alternate function AF4, OPEN-DRAIN, with pull-ups ---
     * Open-drain is mandatory for I2C: a pin may only pull low or release.
     * We enable the internal pull-ups too so the breadboard works even if you
     * forget externals; on the PCB you add 4.7k externals and these are backup. */
    GPIOB->MODER   &= ~((3U << (8 * 2)) | (3U << (9 * 2)));
    GPIOB->MODER   |=  ((2U << (8 * 2)) | (2U << (9 * 2)));   /* 10 = alternate fn */
    GPIOB->OTYPER  |=  ((1U << 8) | (1U << 9));               /* 1  = open-drain   */
    GPIOB->OSPEEDR |=  ((3U << (8 * 2)) | (3U << (9 * 2)));   /* high speed        */
    GPIOB->PUPDR   &= ~((3U << (8 * 2)) | (3U << (9 * 2)));
    GPIOB->PUPDR   |=  ((1U << (8 * 2)) | (1U << (9 * 2)));   /* 01 = pull-up      */
    GPIOB->AFR[1]  &= ~((0xFU << ((8 - 8) * 4)) | (0xFU << ((9 - 8) * 4)));
    GPIOB->AFR[1]  |=  ((4U   << ((8 - 8) * 4)) | (4U   << ((9 - 8) * 4))); /* AF4 */
    /* AFR[1] is the "high" alternate-function register, covering pins 8..15.
     * Pin 8 -> nibble 0, pin 9 -> nibble 1, hence the (pin-8)*4 shift. */

    /* --- peripheral config: software-reset first, then timing --- */
    I2C1->CR1 |=  I2C_CR1_SWRST;                   /* hold peripheral in reset  */
    I2C1->CR1 &= ~I2C_CR1_SWRST;                   /* release: clean known state */

    I2C1->CR2 = (I2C1->CR2 & ~I2C_CR2_FREQ) | 50U; /* tell I2C its input clock is 50 MHz */

    /* CCR for fast mode, duty 16/9:  Tscl = 25 * CCR * Tpclk1.
     * For 400 kHz: Tscl = 2.5 us, Tpclk1 = 20 ns  ->  CCR = 2500/(25*20) = 5. */
    I2C1->CCR = I2C_CCR_FS | I2C_CCR_DUTY | 5U;

    /* TRISE = (max SCL rise / Tpclk1) + 1 = (300 ns / 20 ns) + 1 = 16  (fast mode). */
    I2C1->TRISE = 16U;

    I2C1->CR1 |= I2C_CR1_PE;                        /* enable the peripheral */
}

/* Write one byte VAL into register REG of the device at 7-bit address ADDR7. */
int i2c_write_reg(uint8_t addr7, uint8_t reg, uint8_t val)
{
    WAIT_IDLE();                                    /* bus must be idle */

    I2C1->CR1 |= I2C_CR1_START;                     /* generate START   */
    WAIT(I2C1->SR1 & I2C_SR1_SB);                   /* EV5: START sent  */

    I2C1->DR = (uint8_t)((addr7 << 1) | 0);         /* 7-bit addr + WRITE bit (0) */
    WAIT_ADDR();                                    /* EV6: addr ACKed (NACK -> return -2) */
    (void)I2C1->SR1; (void)I2C1->SR2;               /* clear ADDR: read SR1 THEN SR2 */

    WAIT(I2C1->SR1 & I2C_SR1_TXE);                  /* data register ready */
    I2C1->DR = reg;                                 /* send the register pointer */

    WAIT(I2C1->SR1 & I2C_SR1_TXE);
    I2C1->DR = val;                                 /* send the value */

    WAIT(I2C1->SR1 & I2C_SR1_BTF);                  /* both bytes fully shifted out */
    I2C1->CR1 |= I2C_CR1_STOP;                       /* release the bus */
    return 0;
}

/* Read N bytes starting at register START_REG from device ADDR7 into BUF.
 * Uses write-pointer-then-repeated-START-read. Handles N=1, N=2, N>=3 per RM0383. */
int i2c_read_regs(uint8_t addr7, uint8_t start_reg, uint8_t *buf, uint32_t n)
{
    volatile uint32_t tmp;

    WAIT_IDLE();

    /* ---- Phase A: write the sub-address (which register to start reading) ---- */
    I2C1->CR1 |= I2C_CR1_ACK;                        /* ACK on for the reads to come */
    I2C1->CR1 |= I2C_CR1_START;
    WAIT(I2C1->SR1 & I2C_SR1_SB);
    I2C1->DR = (uint8_t)((addr7 << 1) | 0);          /* address + WRITE */
    WAIT_ADDR();
    tmp = I2C1->SR1; tmp = I2C1->SR2;                /* clear ADDR */
    WAIT(I2C1->SR1 & I2C_SR1_TXE);
    I2C1->DR = start_reg;
    WAIT(I2C1->SR1 & I2C_SR1_BTF);                   /* register pointer fully sent */

    /* ---- Phase B: repeated START, re-address in READ mode ---- */
    I2C1->CR1 |= I2C_CR1_START;                       /* REPEATED start (no STOP between) */
    WAIT(I2C1->SR1 & I2C_SR1_SB);
    I2C1->DR = (uint8_t)((addr7 << 1) | 1);           /* address + READ */

    if (n == 1) {
        /* Single byte: NACK it immediately so the slave sends exactly one byte. */
        WAIT_ADDR();
        I2C1->CR1 &= ~I2C_CR1_ACK;                    /* NACK after this byte */
        tmp = I2C1->SR1; tmp = I2C1->SR2;             /* clear ADDR */
        I2C1->CR1 |= I2C_CR1_STOP;                     /* STOP scheduled now    */
        WAIT(I2C1->SR1 & I2C_SR1_RXNE);
        buf[0] = (uint8_t)I2C1->DR;
    }
    else if (n == 2) {
        /* Two bytes: POS makes the NACK apply to the *next* byte, so the 2nd is NACKed. */
        WAIT_ADDR();
        I2C1->CR1 &= ~I2C_CR1_ACK;
        I2C1->CR1 |=  I2C_CR1_POS;
        tmp = I2C1->SR1; tmp = I2C1->SR2;             /* clear ADDR */
        WAIT(I2C1->SR1 & I2C_SR1_BTF);                /* Data1 in DR, Data2 in shift reg */
        I2C1->CR1 |= I2C_CR1_STOP;
        buf[0] = (uint8_t)I2C1->DR;
        buf[1] = (uint8_t)I2C1->DR;
        I2C1->CR1 &= ~I2C_CR1_POS;                    /* restore POS */
    }
    else {
        /* N >= 3: read with ACK until 3 remain, then the BTF-based tail so the final
         * NACK + STOP land on the right bytes. */
        uint32_t i = 0;
        WAIT_ADDR();
        tmp = I2C1->SR1; tmp = I2C1->SR2;             /* clear ADDR */

        while (n - i > 3) {                            /* all but the last three */
            WAIT(I2C1->SR1 & I2C_SR1_RXNE);
            buf[i++] = (uint8_t)I2C1->DR;              /* ACK is on -> keep reading */
        }
        WAIT(I2C1->SR1 & I2C_SR1_BTF);                /* DataN-2 in DR, DataN-1 in shift */
        I2C1->CR1 &= ~I2C_CR1_ACK;                    /* the byte after next gets NACKed */
        buf[i++] = (uint8_t)I2C1->DR;                 /* read DataN-2 */
        WAIT(I2C1->SR1 & I2C_SR1_BTF);                /* DataN-1 in DR, DataN in shift */
        I2C1->CR1 |= I2C_CR1_STOP;                     /* STOP before pulling last two */
        buf[i++] = (uint8_t)I2C1->DR;                 /* read DataN-1 */
        WAIT(I2C1->SR1 & I2C_SR1_RXNE);
        buf[i++] = (uint8_t)I2C1->DR;                 /* read DataN */
    }

    { uint32_t _to = I2C_TIMEOUT;                      /* wait STOP to clear; don't hang if it sticks */
      while (I2C1->CR1 & I2C_CR1_STOP) { if (--_to == 0u) break; } }
    (void)tmp;
    return 0;
}

/* ===== OLED write helpers + bus recovery (added for the 4-task build) ===== */
/* i2c.c — write a control/register byte, then N payload bytes, in one transaction. */
int i2c_write_buf(uint8_t addr7, uint8_t first, const uint8_t *buf, uint32_t n)
{
 WAIT_IDLE();
 I2C1->CR1 |= I2C_CR1_START;
 WAIT(I2C1->SR1 & I2C_SR1_SB);
 I2C1->DR = (uint8_t)((addr7 << 1) | 0);
 WAIT_ADDR();
 (void)I2C1->SR1; (void)I2C1->SR2; /* clear ADDR (SR1 then SR2) */
 WAIT(I2C1->SR1 & I2C_SR1_TXE);
 I2C1->DR = first; /* the control byte (0x00 or 0x40) */
 for (uint32_t i = 0; i < n; i++) {
 WAIT(I2C1->SR1 & I2C_SR1_TXE);
 I2C1->DR = buf[i];
 }
 WAIT(I2C1->SR1 & I2C_SR1_BTF);
 I2C1->CR1 |= I2C_CR1_STOP;
 return 0;
}
int i2c_write_reg(uint8_t a,uint8_t ctrl,uint8_t v){return i2c_write_buf(a,ctrl,&v,1);}

/* Call when an I2C transfer times out. PB8 = SCL, PB9 = SDA. */
void i2c_bus_recovery(void)
{
 I2C1->CR1 &= ~I2C_CR1_PE; /* disable the peripheral */
 RCC->AHB1ENR |= RCC_AHB1ENR_GPIOBEN;
 /* PB8/PB9 -> open-drain GPIO outputs with pull-ups */
 GPIOB->MODER = (GPIOB->MODER & ~((3U<<16)|(3U<<18))) | (1U<<16)|(1U<<18);
 GPIOB->OTYPER |= (1U<<8)|(1U<<9);
 GPIOB->PUPDR = (GPIOB->PUPDR & ~((3U<<16)|(3U<<18))) | (1U<<16)|(1U<<18);
 GPIOB->BSRR = (1U<<8)|(1U<<9); /* SCL, SDA released high */
 for (int i = 0; i < 9; i++) { /* <=9 clocks frees a slave */
 GPIOB->BSRR = (1U<<(8+16)); /* SCL low */
 for (volatile int d = 0; d < 500; d++) { }
 GPIOB->BSRR = (1U<<8); /* SCL high */
 for (volatile int d = 0; d < 500; d++) { }
 if (GPIOB->IDR & (1U<<9)) break; /* SDA released -> done */
 }
 GPIOB->BSRR = (1U<<(9+16)); /* manual STOP: SDA low... */
 for (volatile int d = 0; d < 500; d++) { }
 GPIOB->BSRR = (1U<<9); /* ...then SDA high */
 for (volatile int d = 0; d < 500; d++) { }
 GPIOB->MODER = (GPIOB->MODER & ~((3U<<16)|(3U<<18))) | (2U<<16)|(2U<<18); /* back to AF */
 I2C1->CR1 |= I2C_CR1_SWRST; /* force peripheral reset */
 I2C1->CR1 &= ~I2C_CR1_SWRST;
 i2c1_init(); /* your section 11.3 init */
}
