#pragma once

#include <stdint.h>
#include <stdlib.h>

#define MICROPY_CONFIG_ROM_LEVEL                (MICROPY_CONFIG_ROM_LEVEL_FULL_FEATURES)

#define MICROPY_ALLOC_PATH_MAX                  (256)
#define MICROPY_PERSISTENT_CODE_LOAD            (1)
#define MICROPY_COMP_ALLOW_TOP_LEVEL_AWAIT      (1)
#define MICROPY_ENABLE_GC                       (1)
#define MICROPY_ENABLE_PYSTACK                  (0)
#define MICROPY_STACK_CHECK                     (0)
#define MICROPY_KBD_EXCEPTION                   (1)
#define MICROPY_REPL_EVENT_DRIVEN               (0)
#define MICROPY_PY_BUILTINS_INPUT               (0)
#define MICROPY_PY_BUILTINS_HELP                (0)
#define MICROPY_LONGINT_IMPL                    (MICROPY_LONGINT_IMPL_MPZ)
#define MICROPY_ENABLE_DOC_STRING               (0)
#define MICROPY_WARNINGS                        (1)
#define MICROPY_ERROR_PRINTER                   (&mp_stderr_print)
#define MICROPY_FLOAT_IMPL                      (MICROPY_FLOAT_IMPL_DOUBLE)
#define MICROPY_USE_INTERNAL_ERRNO              (1)
#define MICROPY_USE_INTERNAL_PRINTF             (0)

/* Cooperative multi-fiber _thread support for main.py + uasyncio loop */
#define MICROPY_PY_THREAD                       (1)
#define MICROPY_PY_THREAD_GIL                   (0)

/* Generator pend_throw needed by bundled uasyncio v2 (uasyncio/core.py) */
#define MICROPY_PY_GENERATOR_PEND_THROW         (1)

/* Time, VFS, and standard library modules */
#define MICROPY_EPOCH_IS_1970                   (1)
#define MICROPY_PY_TIME                         (1)
#define MICROPY_PY_TIME_GMTIME_LOCALTIME_MKTIME (1)
#define MICROPY_PY_TIME_TIME_TIME_NS            (1)
#define MICROPY_PY_TIME_TICKS                   (1)
#define MICROPY_PY_TIME_INCLUDEFILE             "ports/webassembly/modtime.c"
#define MICROPY_PY_RANDOM_SEED_INIT_FUNC        (sc18_random_u32())

#define MICROPY_VFS                             (1)
#define MICROPY_READER_VFS                      (1)
#define MICROPY_VFS_POSIX                       (1)
#define MICROPY_PY_OS                           (1)
#define MICROPY_PY_SELECT                       (0) /* Provided by modsc18.c virtual poller */

#define MICROPY_PY_JS                           (0)
#define MICROPY_PY_JSFFI                        (0)
#define MICROPY_PY_SYS_PLATFORM                 "esp32"

/* Cooperative VM poll hook so sleep-less spin loops (main.py:563) yield per tick */
#define MICROPY_VM_HOOK_COUNT                   (256)
#define MICROPY_VM_HOOK_INIT                    static unsigned int vm_hook_divisor = MICROPY_VM_HOOK_COUNT;
#define MICROPY_VM_HOOK_POLL                    if (--vm_hook_divisor == 0) { \
                                                    vm_hook_divisor = MICROPY_VM_HOOK_COUNT; \
                                                    extern void sc18_vm_poll_hook(void); \
                                                    sc18_vm_poll_hook(); \
                                                }
#define MICROPY_VM_HOOK_LOOP                    MICROPY_VM_HOOK_POLL
#define MICROPY_VM_HOOK_RETURN                  MICROPY_VM_HOOK_POLL

#define MICROPY_EVENT_POLL_HOOK \
    do { \
        extern void mp_handle_pending(bool); \
        mp_handle_pending(true); \
    } while (0);

#ifdef __LP64__
#define MP_SSIZE_MAX                            (0x7fffffffffffffffL)
#define UINT_FMT                                "%lu"
#define INT_FMT                                 "%ld"
typedef intptr_t mp_int_t;
typedef uintptr_t mp_uint_t;
typedef long mp_off_t;
#else
#define MP_SSIZE_MAX                            (0x7fffffff)
#define UINT_FMT                                "%u"
#define INT_FMT                                 "%d"
typedef int mp_int_t;
typedef unsigned int mp_uint_t;
typedef long mp_off_t;
#endif

#define MICROPY_HW_BOARD_NAME                   "SAINTCON 2018 Badge"
#define MICROPY_HW_MCU_NAME                     "ESP32"
#define MP_STATE_PORT                           MP_STATE_VM

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

extern const struct _mp_print_t mp_stderr_print;
uint32_t sc18_random_u32(void);
