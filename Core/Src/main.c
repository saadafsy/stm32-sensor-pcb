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
#include "ssd1306.h"
#include "iwdg.h"
/* The unit that flows sensor -> telemetry through the queue. */
typedef struct {
 int32_t temp_c100; /* 0.01 C */
 uint32_t press_q24_8; /* >>8 = Pa */
 uint32_t hum_q22_10; /* >>10 = %RH */
 uint32_t seq;
} sensor_sample_t;
static QueueHandle_t sample_q; /* producer/consumer FIFO */
static SemaphoreHandle_t uart_mutex; /* owns the UART resource */
static SemaphoreHandle_t i2c_mutex; /* the I2C bus is shared: sensor + display */
static sensor_sample_t g_latest; /* last good reading, shared with the display */
static volatile int g_have_sample = 0;
static TaskHandle_t display_task_handle; /* notified by the button ISR */
static volatile uint8_t g_display_page = 0; /* which OLED page is shown */
static volatile uint32_t g_frames_sent = 0; /* telemetry frames sent (status)*/
static int32_t g_tmin_c100, g_tmax_c100; /* temp min/max (under crit.) */
#define DISPLAY_PAGES 3
/* --- producer: reads the sensor (shares the I2C bus via i2c_mutex) --- */
static void sensor_task(void *arg)
{
 (void)arg;
 int rc = -1;
 for (int attempt = 0; attempt < 3; attempt++) {
 xSemaphoreTake(i2c_mutex, portMAX_DELAY);
 rc = bme280_init();
 if (rc != 0) i2c_bus_recovery(); /* a stuck bus or slow-waking sensor often clears */
 xSemaphoreGive(i2c_mutex);
 if (rc == 0) break;
 vTaskDelay(pdMS_TO_TICKS(50));
 }
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
 xSemaphoreTake(i2c_mutex, portMAX_DELAY);
 int r = bme280_read(&s.temp_c100, &s.press_q24_8, &s.hum_q22_10);
 if (r != 0) i2c_bus_recovery(); /* stuck bus: clock SDA free, SWRST, re-init */
 xSemaphoreGive(i2c_mutex);
 if (r != 0) { vTaskDelay(pdMS_TO_TICKS(250)); continue; } /* don't ship a bad sample */
 s.seq = seq++;
 taskENTER_CRITICAL(); /* hand the latest reading to the display */
 if (!g_have_sample) { g_tmin_c100 = g_tmax_c100 = s.temp_c100; }
 else { if (s.temp_c100 < g_tmin_c100) g_tmin_c100 = s.temp_c100;
 if (s.temp_c100 > g_tmax_c100) g_tmax_c100 = s.temp_c100; }
 g_latest = s; g_have_sample = 1;
 taskEXIT_CRITICAL();
 xQueueSend(sample_q, &s, 0); /* non-blocking: drop if full */
 vTaskDelay(pdMS_TO_TICKS(250)); /* yield CPU for 250 ms */
 }
}
/* --- consumer: blocks on the queue, packs a CRC frame, transmits under the mutex --- */
static void telemetry_task(void *arg)
{
 (void)arg;
 sensor_sample_t s;
 uint8_t frame[FRAME_TOTAL_LEN];
 for (;;) {
 if (xQueueReceive(sample_q, &s, portMAX_DELAY) == pdTRUE) { /* sleep until data */
 uint32_t press_pa = s.press_q24_8 >> 8; /* Q24.8 -> Pa */
 uint32_t hum_c100 = (s.hum_q22_10 * 100u) >> 10; /* Q22.10 -> 0.01 %RH */
 uint32_t n = protocol_build(frame, s.seq, s.temp_c100, press_pa, hum_c100);
 xSemaphoreTake(uart_mutex, portMAX_DELAY);
 for (uint32_t i = 0; i < n; i++) uart_putc((char)frame[i]); /* length-bounded:
 a frame byte may be 0x00, so never use
 uart_write() (it would stop at the 0x00) */
 xSemaphoreGive(uart_mutex);
 g_frames_sent++; /* for the status page */
 }
 }
}
/* ASCII debug option: during bring-up you may prefer a human-readable line you can read
 * in any terminal. Keep an #ifdef DEBUG_ASCII branch that snprintf()s
 * "T=.. H=.. P=.. seq=.." and uart_write()s it instead of the frame. Ship the frame. */
/* --- display: paints the latest reading on the OLED (shares I2C via i2c_mutex).
 * The button ISR notifies this task to flip pages; otherwise it self-refreshes. */
static void display_task(void *arg)
{
 (void)arg;
 xSemaphoreTake(i2c_mutex, portMAX_DELAY);
 ssd1306_init(); /* full driver in section 11.10 */
 xSemaphoreGive(i2c_mutex);

 for (;;) {
 sensor_sample_t s; int have; int32_t tmin, tmax;
 taskENTER_CRITICAL(); /* copy the shared snapshot + min/max quickly */
 s = g_latest; have = g_have_sample; tmin = g_tmin_c100; tmax = g_tmax_c100;
 taskEXIT_CRITICAL();
 uint32_t up = xTaskGetTickCount() / configTICK_RATE_HZ; /* seconds since boot */
 char line[20];
 ssd1306_clear(); /* draw into the RAM framebuffer (no I2C) */
 if (!have) {
 ssd1306_draw_string(0, 0, "warming up...");
 } else if (g_display_page == 0) { /* PAGE 0: live readings */
 int32_t t = s.temp_c100;
 uint32_t pp = (s.press_q24_8 >> 8) / 100u;
 uint32_t hh = s.hum_q22_10 >> 10;
 snprintf(line, sizeof line, "Temp %ld.%01ld C", (long)(t/100), (long)((t%100)/10));
 ssd1306_draw_string(0, 0, line);
 snprintf(line, sizeof line, "Hum %lu %%", (unsigned long)hh); 
ssd1306_draw_string(0, 2, line);
 snprintf(line, sizeof line, "Pres %lu hPa", (unsigned long)pp); 
ssd1306_draw_string(0, 4, line);
 snprintf(line, sizeof line, "seq %lu", (unsigned long)s.seq); 
ssd1306_draw_string(0, 6, line);
 } else if (g_display_page == 1) { /* PAGE 1: status / health */
 ssd1306_draw_string(0, 0, "-- status --");
 snprintf(line, sizeof line, "up %lu s", (unsigned long)up); 
ssd1306_draw_string(0, 2, line);
 snprintf(line, sizeof line, "frames %lu", (unsigned long)g_frames_sent); 
ssd1306_draw_string(0, 4, line);
 snprintf(line, sizeof line, "heap %u", (unsigned)xPortGetFreeHeapSize()); 
ssd1306_draw_string(0, 6, line);
 } else { /* PAGE 2: min / max temp */
 ssd1306_draw_string(0, 0, "-- min/max --");
 snprintf(line, sizeof line, "Tmin %ld.%01ld", (long)(tmin/100), (long)((tmin%100)/
10)); ssd1306_draw_string(0, 2, line);
 snprintf(line, sizeof line, "Tmax %ld.%01ld", (long)(tmax/100), (long)((tmax%100)/
10)); ssd1306_draw_string(0, 4, line);
 snprintf(line, sizeof line, "page %u/%u", (unsigned)(g_display_page+1), 
(unsigned)DISPLAY_PAGES); ssd1306_draw_string(0, 6, line);
 }
 xSemaphoreTake(i2c_mutex, portMAX_DELAY);
 ssd1306_flush(); /* push the framebuffer over I2C */
 xSemaphoreGive(i2c_mutex);
 ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(500)); /* wake on button press, else 2 Hz refresh 
*/
 }
}
/* --- heartbeat: proves the scheduler is preempting --- */
static void heartbeat_task(void *arg)
{
 (void)arg;
 led_init();

 for (;;) {
 led_toggle();
 iwdg_refresh(); /* kick the watchdog from the lowest-priority task 
*/
 vTaskDelay(pdMS_TO_TICKS(500)); /* toggle every 500 ms => 1 Hz blink */
 }
}
/* Button ISR (PA0/EXTI0): the deferred-interrupt pattern. Do the bare minimum here
 * and hand the real work (repaint) to the display task via a task notification. */
void EXTI0_IRQHandler(void)
{
 static TickType_t last = 0;
 if (EXTI->PR & (1U << 0)) {
 EXTI->PR = (1U << 0); /* clear the pending bit (write 1) */
 TickType_t now = xTaskGetTickCountFromISR();
 if ((now - last) >= pdMS_TO_TICKS(200)) { /* software debounce: ignore < 200 ms */
 last = now;
 g_display_page = (uint8_t)((g_display_page + 1) % DISPLAY_PAGES);
 BaseType_t woken = pdFALSE;
 vTaskNotifyGiveFromISR(display_task_handle, &woken);
 portYIELD_FROM_ISR(woken); /* switch to the display task if it outranks 
us */
 }
 }
}
int main(void)
{
 clock_init(); /* 100 MHz first */
 NVIC_SetPriorityGrouping(3); /* all 4 priority bits = preemption, 0 sub-priority: FreeRTOS's 
requirement (NVIC_PRIORITYGROUP_4) */
 usart2_init();
 i2c1_init();
 button_init(); /* PA0 EXTI0 for the display-page button */
 iwdg_init(); /* start the ~1.6 s watchdog; heartbeat_task kicks it */
 uart_write("\r\n[boot] p1 sensor telemetry @100MHz\r\n");
 sample_q = xQueueCreate(8, sizeof(sensor_sample_t));
 uart_mutex = xSemaphoreCreateMutex();
 i2c_mutex = xSemaphoreCreateMutex(); /* shared I2C bus: sensor + display */
 /* priorities: sensor(2) > telemetry(1) = display(1) = heartbeat(1) > idle(0).
 * stacks in WORDS: telemetry and display both use snprintf, so give them room. */
 xTaskCreate(sensor_task, "sensor", 256, NULL, 2, NULL);
 xTaskCreate(telemetry_task, "telem", 256, NULL, 1, NULL);
 xTaskCreate(display_task, "disp", 256, NULL, 1, &display_task_handle);
 xTaskCreate(heartbeat_task, "hb", 128, NULL, 1, NULL);
 vTaskStartScheduler(); /* never returns once tasks are running */
 for (;;) { } /* only reached if the heap was too small to start */
}

void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
 (void)xTask; (void)pcTaskName;
 taskDISABLE_INTERRUPTS();
 /* Breakpoint here: pcTaskName names the culprit task. */
 for (;;) { }
}
void vApplicationMallocFailedHook(void)
{
 taskDISABLE_INTERRUPTS();
 for (;;) { } /* heap exhausted: raise configTOTAL_HEAP_SIZE */
}
