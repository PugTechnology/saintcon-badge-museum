#pragma once

#include "freertos/FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void *TimerHandle_t;
typedef void (*TimerCallbackFunction_t)(TimerHandle_t xTimer);

TimerHandle_t xTimerCreate(const char *const pcTimerName,
                           const TickType_t xTimerPeriodInTicks,
                           const BaseType_t xAutoReload,
                           void *const pvTimerID,
                           TimerCallbackFunction_t pxCallbackFunction);

BaseType_t xTimerStart(TimerHandle_t xTimer, const TickType_t xTicksToWait);
BaseType_t xTimerStop(TimerHandle_t xTimer, const TickType_t xTicksToWait);
BaseType_t xTimerReset(TimerHandle_t xTimer, const TickType_t xTicksToWait);
BaseType_t xTimerChangePeriod(TimerHandle_t xTimer, const TickType_t xNewPeriod, const TickType_t xTicksToWait);
BaseType_t xTimerDelete(TimerHandle_t xTimer, const TickType_t xTicksToWait);
BaseType_t xTimerIsTimerActive(TimerHandle_t xTimer);
void *pvTimerGetTimerID(const TimerHandle_t xTimer);

#ifdef __cplusplus
}
#endif
