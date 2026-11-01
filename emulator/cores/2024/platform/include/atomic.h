#pragma once

#include <stdint.h>

static inline uint32_t Atomic_CompareAndSwap_u32(uint32_t volatile *pDestination,
                                                 uint32_t ulExchange,
                                                 uint32_t ulComparand) {
    if (*pDestination == ulComparand) {
        *pDestination = ulExchange;
        return 1U;
    }
    return 0U;
}

#define ATOMIC_COMPARE_AND_SWAP_SUCCESS 1U
