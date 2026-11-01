#pragma once

#define IRAM_ATTR
#define DRAM_ATTR
#define RTC_DATA_ATTR
#define RTC_RODATA_ATTR
#define RTC_IRAM_ATTR
#define EXT_RAM_ATTR
#define EXT_RAM_BSS_ATTR
#define FORCE_INLINE_ATTR static inline
#define NOINLINE_ATTR __attribute__((noinline))
#define WORD_ALIGNED_ATTR __attribute__((aligned(4)))
#define DMA_ATTR __attribute__((aligned(4)))
