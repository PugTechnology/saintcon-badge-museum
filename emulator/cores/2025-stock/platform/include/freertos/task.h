#pragma once

#include "freertos/FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void *TaskHandle_t;
typedef void (*TaskFunction_t)(void *);

typedef enum {
    eNoAction = 0,
    eSetBits,
    eIncrement,
    eSetValueWithOverwrite,
    eSetValueWithoutOverwrite
} eNotifyAction;

BaseType_t xTaskCreate(TaskFunction_t pxTaskCode,
                       const char *const pcName,
                       const configSTACK_DEPTH_TYPE usStackDepth,
                       void *const pvParameters,
                       UBaseType_t uxPriority,
                       TaskHandle_t *const pxCreatedTask);

BaseType_t xTaskCreatePinnedToCore(TaskFunction_t pxTaskCode,
                                   const char *const pcName,
                                   const uint32_t usStackDepth,
                                   void *const pvParameters,
                                   UBaseType_t uxPriority,
                                   TaskHandle_t *const pxCreatedTask,
                                   const BaseType_t xCoreID);

TaskHandle_t xTaskCreateStatic(TaskFunction_t pxTaskCode,
                               const char *const pcName,
                               const uint32_t ulStackDepth,
                               void *const pvParameters,
                               UBaseType_t uxPriority,
                               StackType_t *const puxStackBuffer,
                               StaticTask_t *const pxTaskBuffer);

TaskHandle_t xTaskCreateStaticPinnedToCore(TaskFunction_t pxTaskCode,
                                           const char *const pcName,
                                           const uint32_t ulStackDepth,
                                           void *const pvParameters,
                                           UBaseType_t uxPriority,
                                           StackType_t *const puxStackBuffer,
                                           StaticTask_t *const pxTaskBuffer,
                                           const BaseType_t xCoreID);

void vTaskDelete(TaskHandle_t xTaskToDelete);
void vTaskDelay(const TickType_t xTicksToDelay);
BaseType_t xTaskDelayUntil(TickType_t *const pxPreviousWakeTime, const TickType_t xTimeIncrement);
#define vTaskDelayUntil(pxPrev, xInc) ((void)xTaskDelayUntil((pxPrev), (xInc)))

TickType_t xTaskGetTickCount(void);
TickType_t xTaskGetTickCountFromISR(void);
TaskHandle_t xTaskGetCurrentTaskHandle(void);
char *pcTaskGetName(TaskHandle_t xTaskToQuery);
UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t xTask);

BaseType_t xTaskNotifyGive(TaskHandle_t xTaskToNotify);
void vTaskNotifyGiveFromISR(TaskHandle_t xTaskToNotify, BaseType_t *pxHigherPriorityTaskWoken);
uint32_t ulTaskNotifyTake(BaseType_t xClearCountOnExit, TickType_t xTicksToWait);
BaseType_t xTaskNotify(TaskHandle_t xTaskToNotify, uint32_t ulValue, eNotifyAction eAction);
BaseType_t xTaskNotifyFromISR(TaskHandle_t xTaskToNotify,
                              uint32_t ulValue,
                              eNotifyAction eAction,
                              BaseType_t *pxHigherPriorityTaskWoken);
BaseType_t xTaskNotifyWait(uint32_t ulBitsToClearOnEntry,
                           uint32_t ulBitsToClearOnExit,
                           uint32_t *pulNotificationValue,
                           TickType_t xTicksToWait);

#ifdef __cplusplus
}
#endif
