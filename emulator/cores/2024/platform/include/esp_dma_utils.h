#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include "esp_err.h"
#include "esp_heap_caps.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ESP_DMA_MALLOC_FLAG_PSRAM (1 << 0)

typedef struct {
    int extra_heap_caps;
    size_t dma_alignment_bytes;
} esp_dma_mem_info_t;

static inline esp_err_t esp_dma_capable_malloc(size_t size,
                                               const esp_dma_mem_info_t *dma_mem_info,
                                               void **out_ptr,
                                               size_t *actual_size) {
    (void)dma_mem_info;
    if (!out_ptr) return ESP_ERR_INVALID_ARG;
    void *p = calloc(1, size);
    if (!p) return ESP_ERR_NO_MEM;
    *out_ptr = p;
    if (actual_size) *actual_size = size;
    return ESP_OK;
}

static inline esp_err_t esp_dma_capable_calloc(size_t calloc_num,
                                               size_t size,
                                               const esp_dma_mem_info_t *dma_mem_info,
                                               void **out_ptr,
                                               size_t *actual_size) {
    return esp_dma_capable_malloc(calloc_num * size, dma_mem_info, out_ptr, actual_size);
}

static inline esp_err_t esp_dma_malloc(size_t size, uint32_t flags, void **out_ptr, size_t *actual_size) {
    (void)flags;
    return esp_dma_capable_malloc(size, NULL, out_ptr, actual_size);
}

#ifdef __cplusplus
}
#endif
