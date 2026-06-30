/* FreeRTOSConfig.h - STM32F411 (Cortex-M4F), ARM_CM4F port.
 * Project-specific values match the guide; the rest is the standard required set. */
#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

/* ---- Scheduler / timing ---- */
#define configCPU_CLOCK_HZ            ( 100000000UL )  /* MUST equal real SYSCLK */
#define configTICK_RATE_HZ            ( 1000 )          /* 1 ms tick             */
#define configUSE_PREEMPTION          1
#define configMAX_PRIORITIES          ( 5 )
#define configMINIMAL_STACK_SIZE      ( 128 )           /* words */
#define configTOTAL_HEAP_SIZE         ( 8 * 1024 )
#define configUSE_MUTEXES             1
#define configUSE_16_BIT_TICKS        0
#define configIDLE_SHOULD_YIELD       1
#define configMAX_TASK_NAME_LEN       ( 16 )
#define configUSE_COUNTING_SEMAPHORES 1
#define configUSE_RECURSIVE_MUTEXES   0
#define configQUEUE_REGISTRY_SIZE     8
#define configUSE_TASK_NOTIFICATIONS  1

/* ---- Hooks / checks (off for this minimal app) ---- */
#define configUSE_IDLE_HOOK           0
#define configUSE_TICK_HOOK           0
#define configCHECK_FOR_STACK_OVERFLOW 0
#define configUSE_MALLOC_FAILED_HOOK  0

/* ---- Memory allocation ---- */
#define configSUPPORT_DYNAMIC_ALLOCATION 1
#define configSUPPORT_STATIC_ALLOCATION  0

/* ---- Software timers (unused here) ---- */
#define configUSE_TIMERS              0

/* ---- Optional API functions linked in ---- */
#define INCLUDE_vTaskPrioritySet      1
#define INCLUDE_uxTaskPriorityGet     1
#define INCLUDE_vTaskDelete           1
#define INCLUDE_vTaskSuspend          1
#define INCLUDE_vTaskDelay            1
#define INCLUDE_vTaskDelayUntil       1
#define INCLUDE_xTaskGetSchedulerState 1

/* ---- Cortex-M interrupt priority wiring (4 priority bits on STM32F4) ---- */
#define configPRIO_BITS                       4
#define configKERNEL_INTERRUPT_PRIORITY       (15 << 4)   /* lowest urgency  */
#define configMAX_SYSCALL_INTERRUPT_PRIORITY  ( 5 << 4)   /* ISRs at >=5 may call *FromISR */
#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY      15
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY 5

/* Route the kernel handlers to the default vector-table names so there is exactly
 * ONE definition of each. Omitting these (while CubeMX also defines them) is the
 * classic "builds but never ticks" failure. */
#define vPortSVCHandler     SVC_Handler
#define xPortPendSVHandler  PendSV_Handler
#define xPortSysTickHandler SysTick_Handler

#endif /* FREERTOS_CONFIG_H */
