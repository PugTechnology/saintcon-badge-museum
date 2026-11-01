#pragma once

#include "freertos/FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void *EventGroupHandle_t;
typedef TickType_t EventBits_t;

EventGroupHandle_t xEventGroupCreate(void);
void vEventGroupDelete(EventGroupHandle_t xEventGroup);
EventBits_t xEventGroupWaitBits(EventGroupHandle_t xEventGroup,
                                const EventBits_t uxBitsToWaitFor,
                                const BaseType_t xClearOnExit,
                                const BaseType_t xWaitForAllBits,
                                TickType_t xTicksToWait);
EventBits_t xEventGroupSetBits(EventGroupHandle_t xEventGroup, const EventBits_t uxBitsToSet);
BaseType_t xEventGroupSetBitsFromISR(EventGroupHandle_t xEventGroup,
                                     const EventBits_t uxBitsToSet,
                                     BaseType_t *pxHigherPriorityTaskWoken);
EventBits_t xEventGroupClearBits(EventGroupHandle_t xEventGroup, const EventBits_t uxBitsToClear);
EventBits_t xEventGroupGetBits(EventGroupHandle_t xEventGroup);

#ifdef __cplusplus
}
#endif
