#include "stm32f4xx.h"
#include <stdio.h>
#include <stdlib.h>
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include "clock.h"
#include "gpio.h"
#include "uart.h"
#include "i2c.h"
#include "bme280.h"
#include "protocol.h"

/* The unit that flows sensor -> telemetry through the queue. */
typedef struct {
    int32_t  temp_c100;     /* 0.01 C  */
    uint32_t press_q24_8;   /* >>8  = Pa */
    uint32_t hum_q22_10;    /* >>10 = %RH */
    uint32_t seq;
} sensor_sample_t;

static QueueHandle_t      sample_q;     /* producer/consumer FIFO  */
static SemaphoreHandle_t  uart_mutex;   /* owns the UART resource  */

/* --- producer: owns I2C, reads the sensor on a fixed cadence --- */
static void sensor_task(void *arg)
{
    (void)arg;
    int rc = bme280_init();
    if (rc != 0) {
        /* Say what kind of failure, once, then idle so the rest of the system stays alive.
         * rc == -2: no ACK on the bus (wiring / pull-ups / address / CSB-not-tied).
         * rc == -1: a device answered but it isn't a BME280 (e.g. 0x58 = BMP280). */
        xSemaphoreTake(uart_mutex, portMAX_DELAY);
        uart_write(rc == -2 ? "[err] BME280: no I2C ACK (check wiring/pull-ups/addr/CSB)\r\n"
                            : "[err] BME280: wrong chip ID (got a BMP280? expected 0x60)\r\n");
        xSemaphoreGive(uart_mutex);
        vTaskDelete(NULL);
    }
    uint32_t seq = 0;
    for (;;) {
        sensor_sample_t s;
        bme280_read(&s.temp_c100, &s.press_q24_8, &s.hum_q22_10);
        s.seq = seq++;
        xQueueSend(sample_q, &s, 0);            /* non-blocking: drop if full */
        vTaskDelay(pdMS_TO_TICKS(250));         /* yield CPU for 250 ms */
    }
}

/* --- consumer: blocks on the queue, packs a CRC frame, transmits under the mutex --- */
static void telemetry_task(void *arg)
{
    (void)arg;
    sensor_sample_t s;
    uint8_t frame[FRAME_TOTAL_LEN];
    for (;;) {
        if (xQueueReceive(sample_q, &s, portMAX_DELAY) == pdTRUE) {  /* sleep until data */
            uint32_t press_pa = s.press_q24_8 >> 8;                  /* Q24.8 -> Pa        */
            uint32_t hum_c100 = (s.hum_q22_10 * 100u) >> 10;         /* Q22.10 -> 0.01 %RH */
            uint32_t n = protocol_build(frame, s.seq, s.temp_c100, press_pa, hum_c100);
            xSemaphoreTake(uart_mutex, portMAX_DELAY);
            for (uint32_t i = 0; i < n; i++) uart_putc((char)frame[i]); /* length-bounded:
                                                  a frame byte may be 0x00, so never use
                                                  uart_write() (it would stop at the 0x00) */
            xSemaphoreGive(uart_mutex);
        }
    }
}
/* ASCII debug option: during bring-up you may prefer a human-readable line you can read
 * in any terminal. Keep an #ifdef DEBUG_ASCII branch that snprintf()s
 * "T=.. H=.. P=.. seq=.." and uart_write()s it instead of the frame. Ship the frame. */

/* --- heartbeat: proves the scheduler is preempting --- */
static void heartbeat_task(void *arg)
{
    (void)arg;
    led_init();
    for (;;) {
        led_toggle();
        vTaskDelay(pdMS_TO_TICKS(500));         /* toggle every 500 ms => 1 Hz blink */
    }
}

int main(void)
{
    clock_init();        /* 100 MHz first */
    usart2_init();
    i2c1_init();

    uart_write("\r\n[boot] p1 sensor telemetry @100MHz\r\n");

    sample_q   = xQueueCreate(8, sizeof(sensor_sample_t));
    uart_mutex = xSemaphoreCreateMutex();

    /* priorities: sensor(2) > telemetry(1) = heartbeat(1) > idle(0).
     * stacks in WORDS: telemetry needs the most (snprintf is stack-hungry). */
    xTaskCreate(sensor_task,    "sensor", 256, NULL, 2, NULL);
    xTaskCreate(telemetry_task, "telem",  256, NULL, 1, NULL);
    xTaskCreate(heartbeat_task, "hb",     128, NULL, 1, NULL);

    vTaskStartScheduler();   /* never returns once tasks are running */
    for (;;) { }             /* only reached if the heap was too small to start */
}
