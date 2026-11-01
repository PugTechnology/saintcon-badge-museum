#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include "sdkconfig.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef int BaseType_t;
typedef unsigned int UBaseType_t;
typedef uint32_t TickType_t;
typedef uint8_t StackType_t;
typedef uint32_t configSTACK_DEPTH_TYPE;

typedef struct {
    uint32_t dummy[8];
} StaticTask_t;

typedef struct {
    uint32_t dummy[4];
} StaticSemaphore_t;

typedef struct {
    uint32_t dummy[4];
} StaticQueue_t;

typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0

#define pdFALSE ((BaseType_t)0)
#define pdTRUE  ((BaseType_t)1)
#define pdPASS  pdTRUE
#define pdFAIL  pdFALSE

#define errQUEUE_EMPTY ((BaseType_t)0)
#define errQUEUE_FULL  ((BaseType_t)0)

#define portMAX_DELAY ((TickType_t)0xFFFFFFFFUL)
#define portTICK_PERIOD_MS ((TickType_t)(1000 / CONFIG_FREERTOS_HZ))
#define pdMS_TO_TICKS(xTimeInMs) ((TickType_t)(((uint64_t)(xTimeInMs) * (uint64_t)CONFIG_FREERTOS_HZ) / 1000ULL))
#define pdTICKS_TO_MS(xTicks) ((uint32_t)(((uint64_t)(xTicks) * 1000ULL) / (uint64_t)CONFIG_FREERTOS_HZ))

#define tskIDLE_PRIORITY ((UBaseType_t)0U)
#define tskNO_AFFINITY   ((BaseType_t)0x7FFFFFFF)
#define configMAX_PRIORITIES 25

#define taskENTER_CRITICAL(...) ((void)0)
#define taskEXIT_CRITICAL(...)  ((void)0)
#define taskENTER_CRITICAL_FROM_ISR() 0U
#define taskEXIT_CRITICAL_FROM_ISR(x) ((void)(x))
#define portENTER_CRITICAL(...) ((void)0)
#define portEXIT_CRITICAL(...)  ((void)0)
#define portYIELD() ((void)0)
#define portYIELD_FROM_ISR(...) ((void)0)

#define pvPortMalloc malloc
#define vPortFree free

#ifndef BIT
#define BIT(nr) (1UL << (nr))
#endif
#define BIT0  (1UL << 0)
#define BIT1  (1UL << 1)
#define BIT2  (1UL << 2)
#define BIT3  (1UL << 3)
#define BIT4  (1UL << 4)
#define BIT5  (1UL << 5)
#define BIT6  (1UL << 6)
#define BIT7  (1UL << 7)
#define BIT8  (1UL << 8)
#define BIT9  (1UL << 9)
#define BIT10 (1UL << 10)
#define BIT11 (1UL << 11)
#define BIT12 (1UL << 12)
#define BIT13 (1UL << 13)
#define BIT14 (1UL << 14)
#define BIT15 (1UL << 15)

#ifdef __cplusplus
}
#endif

#include "esp_cpu.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "freertos/event_groups.h"

