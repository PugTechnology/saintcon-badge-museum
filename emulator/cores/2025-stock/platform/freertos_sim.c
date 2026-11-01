#define _DEFAULT_SOURCE
#define _XOPEN_SOURCE 700

#include "platform_internal.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/timers.h"
#include "freertos/event_groups.h"
#include "esp_timer.h"
#include "esp_log.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/fiber.h>
#else
#include <ucontext.h>
#endif

/* ============================================================================
 * Limits & Constants
 * ============================================================================ */

#define MAX_TASKS        24
#define TASK_C_STACK_SZ  (192 * 1024)
#define TASK_AS_STACK_SZ (128 * 1024)

#define MAX_SEMAPHORES   160
#define MAX_QUEUES       48
#define MAX_EVENT_GROUPS 32
#define MAX_SW_TIMERS    32
#define MAX_ESP_TIMERS   48

typedef enum {
    TASK_UNUSED = 0,
    TASK_READY,
    TASK_SLEEPING,
    TASK_WAIT_SEM,
    TASK_WAIT_QUEUE_RECV,
    TASK_WAIT_QUEUE_SEND,
    TASK_WAIT_EVENT,
    TASK_WAIT_NOTIFY,
    TASK_ZOMBIE,
} sim_task_state_t;

typedef struct sim_task {
    sim_task_state_t state;
    char name[24];
    TaskFunction_t entry;
    void *arg;
    UBaseType_t priority;
    bool started;
    uint64_t wake_us;
    bool wait_succeeded;

    /* Object wait state */
    void *wait_obj;
    const void *queue_send_item;
    void *queue_recv_buf;
    bool queue_send_front;

    /* EventGroup wait state */
    EventBits_t eg_wait_bits;
    bool eg_wait_all;
    bool eg_clear_on_exit;
    EventBits_t eg_result_bits;

    /* Direct-to-task notification state */
    uint32_t notify_val;
    bool notify_pending;

#ifdef __EMSCRIPTEN__
    emscripten_fiber_t fiber;
#else
    ucontext_t ctx;
#endif
} sim_task_t;

typedef enum {
    SEM_UNUSED = 0,
    SEM_BINARY,
    SEM_COUNTING,
    SEM_MUTEX,
    SEM_RECURSIVE_MUTEX,
} sim_sem_type_t;

typedef struct {
    sim_sem_type_t type;
    bool persistent;
    UBaseType_t count;
    UBaseType_t max_count;
    int owner_task;
    uint32_t recursion;
} sim_sem_t;

typedef struct {
    bool used;
    bool persistent;
    UBaseType_t length;
    UBaseType_t item_size;
    UBaseType_t head;
    UBaseType_t tail;
    UBaseType_t count;
    uint8_t *storage;
} sim_queue_t;

typedef struct {
    bool used;
    bool persistent;
    EventBits_t bits;
} sim_event_group_t;

typedef struct {
    bool used;
    bool active;
    bool auto_reload;
    char name[24];
    uint64_t period_us;
    uint64_t next_fire_us;
    void *timer_id;
    TimerCallbackFunction_t callback;
} sim_sw_timer_t;

struct esp_timer {
    bool used;
    bool active;
    bool periodic;
    char name[32];
    uint64_t period_us;
    uint64_t next_fire_us;
    esp_timer_cb_t callback;
    void *arg;
};

/* ============================================================================
 * Static Storage
 * ============================================================================ */

static bool s_scheduler_initialized = false;
static uint64_t s_now_us = 10000ULL;
static int s_current_task = -1;
static int s_rr_cursor = 0;
static int s_timer_svc_idx = -1;

static sim_task_t s_tasks[MAX_TASKS];
_Alignas(16) static uint8_t s_task_c_stacks[MAX_TASKS][TASK_C_STACK_SZ];

#ifdef __EMSCRIPTEN__
_Alignas(16) static uint8_t s_task_async_stacks[MAX_TASKS][TASK_AS_STACK_SZ];
_Alignas(16) static uint8_t s_sched_async_stack[32 * 1024];
static emscripten_fiber_t s_sched_fiber;
static bool s_sched_fiber_inited = false;
#else
static ucontext_t s_sched_ctx;
#endif

static sim_sem_t s_sems[MAX_SEMAPHORES];
static sim_queue_t s_queues[MAX_QUEUES];
static sim_event_group_t s_event_groups[MAX_EVENT_GROUPS];
static sim_sw_timer_t s_sw_timers[MAX_SW_TIMERS];
static struct esp_timer s_esp_timers[MAX_ESP_TIMERS];

/* ============================================================================
 * Low-level Fiber Context Switching
 * ============================================================================ */

static void task_trampoline_idx(int idx) {
    sim_task_t *t = &s_tasks[idx];
    if (t->entry) {
        t->entry(t->arg);
    }
    t->state = TASK_ZOMBIE;
#ifdef __EMSCRIPTEN__
    emscripten_fiber_swap(&t->fiber, &s_sched_fiber);
#else
    swapcontext(&t->ctx, &s_sched_ctx);
#endif
}

#ifdef __EMSCRIPTEN__
static void task_fiber_entry(void *arg) {
    int idx = (int)(intptr_t)arg;
    task_trampoline_idx(idx);
}
#else
static void task_ucontext_entry(int idx) {
    task_trampoline_idx(idx);
}
#endif

static void init_task_context(int idx) {
#ifdef __EMSCRIPTEN__
    emscripten_fiber_init(
        &s_tasks[idx].fiber,
        task_fiber_entry,
        (void *)(intptr_t)idx,
        s_task_c_stacks[idx],
        TASK_C_STACK_SZ,
        s_task_async_stacks[idx],
        TASK_AS_STACK_SZ);
#else
    getcontext(&s_tasks[idx].ctx);
    s_tasks[idx].ctx.uc_stack.ss_sp = s_task_c_stacks[idx];
    s_tasks[idx].ctx.uc_stack.ss_size = TASK_C_STACK_SZ;
    s_tasks[idx].ctx.uc_link = &s_sched_ctx;
    makecontext(&s_tasks[idx].ctx, (void (*)(void))task_ucontext_entry, 1, idx);
#endif
}

static void yield_to_scheduler(void) {
    int me = s_current_task;
    if (me < 0 || me >= MAX_TASKS) {
        return;
    }
#ifdef __EMSCRIPTEN__
    emscripten_fiber_swap(&s_tasks[me].fiber, &s_sched_fiber);
#else
    swapcontext(&s_tasks[me].ctx, &s_sched_ctx);
#endif
}

static uint64_t ticks_to_deadline_us(TickType_t ticks) {
    if (ticks == 0) {
        return s_now_us;
    }
    if (ticks == portMAX_DELAY) {
        return UINT64_MAX;
    }
    uint64_t us = ((uint64_t)ticks * 1000000ULL) / (uint64_t)CONFIG_FREERTOS_HZ;
    if (us == 0) us = 1000ULL;
    return s_now_us + us;
}

static void wake_timer_svc_if_needed(void) {
    if (s_timer_svc_idx >= 0 && s_tasks[s_timer_svc_idx].state == TASK_SLEEPING) {
        s_tasks[s_timer_svc_idx].state = TASK_READY;
    }
}

/* ============================================================================
 * Built-in Timer Service Task (executes esp_timer & FreeRTOS xTimer callbacks)
 * ============================================================================ */

static bool any_timer_due_now(void) {
    for (int i = 0; i < MAX_ESP_TIMERS; i++) {
        if (s_esp_timers[i].used && s_esp_timers[i].active && s_esp_timers[i].next_fire_us <= s_now_us) {
            return true;
        }
    }
    for (int i = 0; i < MAX_SW_TIMERS; i++) {
        if (s_sw_timers[i].used && s_sw_timers[i].active && s_sw_timers[i].next_fire_us <= s_now_us) {
            return true;
        }
    }
    return false;
}

static void timer_service_task(void *arg) {
    (void)arg;
    while (1) {
        bool fired = false;

        /* Fire earliest due esp_timer */
        int best_esp = -1;
        uint64_t best_esp_us = UINT64_MAX;
        for (int i = 0; i < MAX_ESP_TIMERS; i++) {
            if (s_esp_timers[i].used && s_esp_timers[i].active && s_esp_timers[i].next_fire_us <= s_now_us) {
                if (s_esp_timers[i].next_fire_us < best_esp_us) {
                    best_esp_us = s_esp_timers[i].next_fire_us;
                    best_esp = i;
                }
            }
        }
        if (best_esp >= 0) {
            struct esp_timer *et = &s_esp_timers[best_esp];
            esp_timer_cb_t cb = et->callback;
            void *cb_arg = et->arg;
            if (et->periodic) {
                et->next_fire_us = s_now_us + et->period_us;
            } else {
                et->active = false;
            }
            if (cb) {
                cb(cb_arg);
            }
            fired = true;
        }

        /* Fire earliest due FreeRTOS software timer */
        int best_sw = -1;
        uint64_t best_sw_us = UINT64_MAX;
        for (int i = 0; i < MAX_SW_TIMERS; i++) {
            if (s_sw_timers[i].used && s_sw_timers[i].active && s_sw_timers[i].next_fire_us <= s_now_us) {
                if (s_sw_timers[i].next_fire_us < best_sw_us) {
                    best_sw_us = s_sw_timers[i].next_fire_us;
                    best_sw = i;
                }
            }
        }
        if (best_sw >= 0) {
            sim_sw_timer_t *st = &s_sw_timers[best_sw];
            TimerCallbackFunction_t cb = st->callback;
            if (st->auto_reload) {
                st->next_fire_us = s_now_us + st->period_us;
            } else {
                st->active = false;
            }
            if (cb) {
                cb((TimerHandle_t)st);
            }
            fired = true;
        }

        if (!fired) {
            /* Sleep until woken by scheduler when a timer becomes due */
            s_tasks[s_timer_svc_idx].state = TASK_SLEEPING;
            s_tasks[s_timer_svc_idx].wake_us = UINT64_MAX;
            yield_to_scheduler();
        }
    }
}

/* ============================================================================
 * Main Cooperative Scheduler Lifecycle
 * ============================================================================ */

static void app_main_task_wrapper(void *arg) {
    (void)arg;
    app_main();
}

void platform_scheduler_init(void) {
#ifdef __EMSCRIPTEN__
    if (!s_sched_fiber_inited) {
        emscripten_fiber_init_from_current_context(
            &s_sched_fiber, s_sched_async_stack, sizeof(s_sched_async_stack));
        s_sched_fiber_inited = true;
    }
#endif

    s_now_us = 10000ULL;
    s_current_task = -1;
    s_rr_cursor = 0;
    s_timer_svc_idx = -1;

    memset(s_tasks, 0, sizeof(s_tasks));

    for (int i = 0; i < MAX_SEMAPHORES; i++) {
        if (s_sems[i].persistent && s_sems[i].type != SEM_UNUSED) {
            s_sems[i].owner_task = -1;
            s_sems[i].recursion = 0;
            if (s_sems[i].type == SEM_MUTEX || s_sems[i].type == SEM_RECURSIVE_MUTEX) {
                s_sems[i].count = 1;
            }
        } else {
            memset(&s_sems[i], 0, sizeof(s_sems[i]));
        }
    }

    for (int i = 0; i < MAX_QUEUES; i++) {
        if (!s_queues[i].persistent) {
            if (s_queues[i].storage) {
                free(s_queues[i].storage);
            }
            memset(&s_queues[i], 0, sizeof(s_queues[i]));
        } else {
            s_queues[i].head = 0;
            s_queues[i].tail = 0;
            s_queues[i].count = 0;
        }
    }

    for (int i = 0; i < MAX_EVENT_GROUPS; i++) {
        if (!s_event_groups[i].persistent) {
            memset(&s_event_groups[i], 0, sizeof(s_event_groups[i]));
        } else {
            s_event_groups[i].bits = 0;
        }
    }

    memset(s_sw_timers, 0, sizeof(s_sw_timers));
    memset(s_esp_timers, 0, sizeof(s_esp_timers));

    s_scheduler_initialized = true;

    /* Create high-priority timer service task */
    TaskHandle_t timer_handle = NULL;
    xTaskCreate(timer_service_task, "timer_svc", 4096, NULL, configMAX_PRIORITIES - 1, &timer_handle);
    for (int i = 0; i < MAX_TASKS; i++) {
        if (&s_tasks[i] == timer_handle) {
            s_timer_svc_idx = i;
            break;
        }
    }
}

void platform_scheduler_start_app_main(void) {
    xTaskCreate(app_main_task_wrapper, "main", 8192, NULL, 1, NULL);
}

uint64_t platform_now_us(void) {
    return s_now_us;
}

static void cleanup_zombies(void) {
    for (int i = 0; i < MAX_TASKS; i++) {
        if (s_tasks[i].state == TASK_ZOMBIE) {
            memset(&s_tasks[i], 0, sizeof(s_tasks[i]));
            s_tasks[i].state = TASK_UNUSED;
        }
    }
}

static void wake_expired_waiters(void) {
    for (int i = 0; i < MAX_TASKS; i++) {
        if (i == s_timer_svc_idx) continue;
        sim_task_t *t = &s_tasks[i];
        if (t->state == TASK_SLEEPING && t->wake_us <= s_now_us) {
            t->state = TASK_READY;
        } else if ((t->state == TASK_WAIT_SEM ||
                    t->state == TASK_WAIT_QUEUE_RECV ||
                    t->state == TASK_WAIT_QUEUE_SEND ||
                    t->state == TASK_WAIT_EVENT ||
                    t->state == TASK_WAIT_NOTIFY) &&
                   t->wake_us <= s_now_us) {
            t->wait_succeeded = false;
            t->wait_obj = NULL;
            t->state = TASK_READY;
        }
    }
    if (any_timer_due_now()) {
        wake_timer_svc_if_needed();
    }
}

static int pick_next_ready_task(void) {
    int best_idx = -1;
    UBaseType_t best_prio = 0;
    for (int step = 0; step < MAX_TASKS; step++) {
        int idx = (s_rr_cursor + step) % MAX_TASKS;
        if (s_tasks[idx].state == TASK_READY) {
            if (best_idx < 0 || s_tasks[idx].priority > best_prio) {
                best_idx = idx;
                best_prio = s_tasks[idx].priority;
            }
        }
    }
    if (best_idx >= 0) {
        s_rr_cursor = (best_idx + 1) % MAX_TASKS;
    }
    return best_idx;
}

static uint64_t next_scheduled_wake_us(uint64_t cap_us) {
    uint64_t earliest = cap_us;
    for (int i = 0; i < MAX_TASKS; i++) {
        if (i == s_timer_svc_idx) continue;
        sim_task_t *t = &s_tasks[i];
        if ((t->state == TASK_SLEEPING ||
             t->state == TASK_WAIT_SEM ||
             t->state == TASK_WAIT_QUEUE_RECV ||
             t->state == TASK_WAIT_QUEUE_SEND ||
             t->state == TASK_WAIT_EVENT ||
             t->state == TASK_WAIT_NOTIFY) &&
            t->wake_us < earliest) {
            earliest = t->wake_us;
        }
    }
    for (int i = 0; i < MAX_ESP_TIMERS; i++) {
        if (s_esp_timers[i].used && s_esp_timers[i].active && s_esp_timers[i].next_fire_us < earliest) {
            earliest = s_esp_timers[i].next_fire_us;
        }
    }
    for (int i = 0; i < MAX_SW_TIMERS; i++) {
        if (s_sw_timers[i].used && s_sw_timers[i].active && s_sw_timers[i].next_fire_us < earliest) {
            earliest = s_sw_timers[i].next_fire_us;
        }
    }
    return earliest;
}

void platform_scheduler_step(uint64_t delta_us) {
    uint64_t end_us = s_now_us + delta_us;
    uint32_t same_tick_runs = 0;
    uint64_t last_us = s_now_us;

    while (1) {
        cleanup_zombies();
        wake_expired_waiters();

        int next_idx = pick_next_ready_task();
        if (next_idx >= 0) {
            if (!s_tasks[next_idx].started) {
                init_task_context(next_idx);
                s_tasks[next_idx].started = true;
            }
            s_current_task = next_idx;
#ifdef __EMSCRIPTEN__
            emscripten_fiber_swap(&s_sched_fiber, &s_tasks[next_idx].fiber);
#else
            swapcontext(&s_sched_ctx, &s_tasks[next_idx].ctx);
#endif
            s_current_task = -1;

            if (s_now_us == last_us) {
                same_tick_runs++;
                if (same_tick_runs > 4096) {
                    s_now_us += 250ULL;
                    last_us = s_now_us;
                    same_tick_runs = 0;
                    if (s_now_us > end_us) {
                        break;
                    }
                }
            } else {
                last_us = s_now_us;
                same_tick_runs = 0;
            }
            continue;
        }

        if (s_now_us >= end_us) {
            break;
        }

        uint64_t jump_us = next_scheduled_wake_us(end_us);
        if (jump_us < s_now_us) {
            jump_us = s_now_us;
        }
        s_now_us = jump_us;
        last_us = s_now_us;
        same_tick_runs = 0;

        /* If we jumped straight to end_us with nothing due at end_us, do one final check then exit */
        wake_expired_waiters();
        if (pick_next_ready_task() < 0 && s_now_us >= end_us) {
            break;
        }
    }

    cleanup_zombies();
}

/* ============================================================================
 * FreeRTOS Task API
 * ============================================================================ */

static BaseType_t create_task_internal(TaskFunction_t pxTaskCode,
                                       const char *const pcName,
                                       UBaseType_t uxPriority,
                                       TaskHandle_t *const pxCreatedTask) {
    if (!pxTaskCode) return pdFAIL;
    cleanup_zombies();

    for (int i = 0; i < MAX_TASKS; i++) {
        if (s_tasks[i].state == TASK_UNUSED) {
            memset(&s_tasks[i], 0, sizeof(s_tasks[i]));
            s_tasks[i].state = TASK_READY;
            s_tasks[i].entry = pxTaskCode;
            s_tasks[i].priority = uxPriority;
            s_tasks[i].started = false;
            snprintf(s_tasks[i].name, sizeof(s_tasks[i].name), "%s", pcName ? pcName : "task");
            if (pxCreatedTask) {
                *pxCreatedTask = (TaskHandle_t)&s_tasks[i];
            }
            return pdPASS;
        }
    }
    fprintf(stderr, "[freertos_sim] ERROR: Out of task slots creating '%s'\n", pcName ? pcName : "?");
    return pdFAIL;
}

BaseType_t xTaskCreate(TaskFunction_t pxTaskCode,
                       const char *const pcName,
                       const configSTACK_DEPTH_TYPE usStackDepth,
                       void *const pvParameters,
                       UBaseType_t uxPriority,
                       TaskHandle_t *const pxCreatedTask) {
    (void)usStackDepth;
    TaskHandle_t h = NULL;
    BaseType_t rc = create_task_internal(pxTaskCode, pcName, uxPriority, &h);
    if (rc == pdPASS && h) {
        ((sim_task_t *)h)->arg = pvParameters;
        if (pxCreatedTask) *pxCreatedTask = h;
    }
    return rc;
}

BaseType_t xTaskCreatePinnedToCore(TaskFunction_t pxTaskCode,
                                   const char *const pcName,
                                   const uint32_t usStackDepth,
                                   void *const pvParameters,
                                   UBaseType_t uxPriority,
                                   TaskHandle_t *const pxCreatedTask,
                                   const BaseType_t xCoreID) {
    (void)xCoreID;
    return xTaskCreate(pxTaskCode, pcName, usStackDepth, pvParameters, uxPriority, pxCreatedTask);
}

TaskHandle_t xTaskCreateStatic(TaskFunction_t pxTaskCode,
                               const char *const pcName,
                               const uint32_t ulStackDepth,
                               void *const pvParameters,
                               UBaseType_t uxPriority,
                               StackType_t *const puxStackBuffer,
                               StaticTask_t *const pxTaskBuffer) {
    (void)ulStackDepth;
    (void)puxStackBuffer;
    (void)pxTaskBuffer;
    TaskHandle_t h = NULL;
    if (xTaskCreate(pxTaskCode, pcName, ulStackDepth, pvParameters, uxPriority, &h) == pdPASS) {
        return h;
    }
    return NULL;
}

TaskHandle_t xTaskCreateStaticPinnedToCore(TaskFunction_t pxTaskCode,
                                           const char *const pcName,
                                           const uint32_t ulStackDepth,
                                           void *const pvParameters,
                                           UBaseType_t uxPriority,
                                           StackType_t *const puxStackBuffer,
                                           StaticTask_t *const pxTaskBuffer,
                                           const BaseType_t xCoreID) {
    (void)xCoreID;
    return xTaskCreateStatic(pxTaskCode, pcName, ulStackDepth, pvParameters, uxPriority, puxStackBuffer, pxTaskBuffer);
}

void vTaskDelete(TaskHandle_t xTaskToDelete) {
    sim_task_t *target = (sim_task_t *)xTaskToDelete;
    if (target == NULL) {
        if (s_current_task >= 0) {
            s_tasks[s_current_task].state = TASK_ZOMBIE;
            yield_to_scheduler();
        }
        return;
    }
    int idx = (int)(target - s_tasks);
    if (idx < 0 || idx >= MAX_TASKS) return;
    if (idx == s_current_task) {
        s_tasks[idx].state = TASK_ZOMBIE;
        yield_to_scheduler();
    } else {
        memset(&s_tasks[idx], 0, sizeof(s_tasks[idx]));
        s_tasks[idx].state = TASK_UNUSED;
    }
}

void vTaskDelay(const TickType_t xTicksToDelay) {
    if (s_current_task < 0) return;
    if (xTicksToDelay == 0) {
        yield_to_scheduler();
        return;
    }
    s_tasks[s_current_task].state = TASK_SLEEPING;
    s_tasks[s_current_task].wake_us = ticks_to_deadline_us(xTicksToDelay);
    yield_to_scheduler();
}

BaseType_t xTaskDelayUntil(TickType_t *const pxPreviousWakeTime, const TickType_t xTimeIncrement) {
    TickType_t now = xTaskGetTickCount();
    if (!pxPreviousWakeTime) {
        vTaskDelay(xTimeIncrement);
        return pdTRUE;
    }
    TickType_t target = *pxPreviousWakeTime + xTimeIncrement;
    *pxPreviousWakeTime = target;
    int32_t diff = (int32_t)(target - now);
    if (diff > 0) {
        vTaskDelay((TickType_t)diff);
        return pdTRUE;
    }
    return pdFALSE;
}

TickType_t xTaskGetTickCount(void) {
    return (TickType_t)((s_now_us * (uint64_t)CONFIG_FREERTOS_HZ) / 1000000ULL);
}

TickType_t xTaskGetTickCountFromISR(void) {
    return xTaskGetTickCount();
}

TaskHandle_t xTaskGetCurrentTaskHandle(void) {
    if (s_current_task >= 0 && s_current_task < MAX_TASKS) {
        return (TaskHandle_t)&s_tasks[s_current_task];
    }
    return NULL;
}

char *pcTaskGetName(TaskHandle_t xTaskToQuery) {
    sim_task_t *t = (sim_task_t *)xTaskToQuery;
    if (!t) {
        if (s_current_task >= 0) return s_tasks[s_current_task].name;
        return (char *)"sched";
    }
    return t->name;
}

UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t xTask) {
    (void)xTask;
    return 16384U;
}

/* ============================================================================
 * Direct-to-Task Notifications
 * ============================================================================ */

BaseType_t xTaskNotify(TaskHandle_t xTaskToNotify, uint32_t ulValue, eNotifyAction eAction) {
    sim_task_t *t = (sim_task_t *)xTaskToNotify;
    if (!t) return pdFAIL;
    switch (eAction) {
        case eNoAction:
            break;
        case eSetBits:
            t->notify_val |= ulValue;
            break;
        case eIncrement:
            t->notify_val++;
            break;
        case eSetValueWithOverwrite:
            t->notify_val = ulValue;
            break;
        case eSetValueWithoutOverwrite:
            if (t->notify_pending) return pdFAIL;
            t->notify_val = ulValue;
            break;
    }
    t->notify_pending = true;
    if (t->state == TASK_WAIT_NOTIFY) {
        t->wait_succeeded = true;
        t->state = TASK_READY;
    }
    return pdPASS;
}

BaseType_t xTaskNotifyFromISR(TaskHandle_t xTaskToNotify,
                              uint32_t ulValue,
                              eNotifyAction eAction,
                              BaseType_t *pxHigherPriorityTaskWoken) {
    if (pxHigherPriorityTaskWoken) *pxHigherPriorityTaskWoken = pdFALSE;
    return xTaskNotify(xTaskToNotify, ulValue, eAction);
}

BaseType_t xTaskNotifyGive(TaskHandle_t xTaskToNotify) {
    return xTaskNotify(xTaskToNotify, 0, eIncrement);
}

void vTaskNotifyGiveFromISR(TaskHandle_t xTaskToNotify, BaseType_t *pxHigherPriorityTaskWoken) {
    if (pxHigherPriorityTaskWoken) *pxHigherPriorityTaskWoken = pdFALSE;
    xTaskNotifyGive(xTaskToNotify);
}

uint32_t ulTaskNotifyTake(BaseType_t xClearCountOnExit, TickType_t xTicksToWait) {
    if (s_current_task < 0) return 0;
    sim_task_t *me = &s_tasks[s_current_task];
    if (me->notify_val == 0 && xTicksToWait > 0) {
        me->state = TASK_WAIT_NOTIFY;
        me->wake_us = ticks_to_deadline_us(xTicksToWait);
        me->wait_succeeded = false;
        yield_to_scheduler();
    }
    uint32_t val = me->notify_val;
    if (val > 0) {
        if (xClearCountOnExit != pdFALSE) {
            me->notify_val = 0;
            me->notify_pending = false;
        } else {
            me->notify_val--;
            if (me->notify_val == 0) me->notify_pending = false;
        }
    } else {
        me->notify_pending = false;
    }
    return val;
}

BaseType_t xTaskNotifyWait(uint32_t ulBitsToClearOnEntry,
                           uint32_t ulBitsToClearOnExit,
                           uint32_t *pulNotificationValue,
                           TickType_t xTicksToWait) {
    if (s_current_task < 0) return pdFALSE;
    sim_task_t *me = &s_tasks[s_current_task];
    if (!me->notify_pending) {
        me->notify_val &= ~ulBitsToClearOnEntry;
        if (xTicksToWait == 0) return pdFALSE;
        me->state = TASK_WAIT_NOTIFY;
        me->wake_us = ticks_to_deadline_us(xTicksToWait);
        me->wait_succeeded = false;
        yield_to_scheduler();
        if (!me->notify_pending) return pdFALSE;
    }
    if (pulNotificationValue) *pulNotificationValue = me->notify_val;
    me->notify_val &= ~ulBitsToClearOnExit;
    me->notify_pending = false;
    return pdTRUE;
}

/* ============================================================================
 * Semaphores & Mutexes (supports static-init mutexes before emu_init!)
 * ============================================================================ */

static sim_sem_t *alloc_sem(sim_sem_type_t type, UBaseType_t init_count, UBaseType_t max_count) {
    for (int i = 0; i < MAX_SEMAPHORES; i++) {
        if (s_sems[i].type == SEM_UNUSED) {
            s_sems[i].type = type;
            s_sems[i].persistent = !s_scheduler_initialized;
            s_sems[i].count = init_count;
            s_sems[i].max_count = max_count;
            s_sems[i].owner_task = -1;
            s_sems[i].recursion = 0;
            return &s_sems[i];
        }
    }
    fprintf(stderr, "[freertos_sim] ERROR: Out of semaphore slots\n");
    return NULL;
}

SemaphoreHandle_t xSemaphoreCreateBinary(void) {
    return (SemaphoreHandle_t)alloc_sem(SEM_BINARY, 0, 1);
}

SemaphoreHandle_t xSemaphoreCreateCounting(UBaseType_t uxMaxCount, UBaseType_t uxInitialCount) {
    return (SemaphoreHandle_t)alloc_sem(SEM_COUNTING, uxInitialCount, uxMaxCount);
}

SemaphoreHandle_t xSemaphoreCreateMutex(void) {
    return (SemaphoreHandle_t)alloc_sem(SEM_MUTEX, 1, 1);
}

SemaphoreHandle_t xSemaphoreCreateRecursiveMutex(void) {
    return (SemaphoreHandle_t)alloc_sem(SEM_RECURSIVE_MUTEX, 1, 1);
}

void vSemaphoreDelete(SemaphoreHandle_t xSemaphore) {
    sim_sem_t *s = (sim_sem_t *)xSemaphore;
    if (!s || s->persistent) return;
    memset(s, 0, sizeof(*s));
}

static void wake_highest_waiter_for_sem(sim_sem_t *s) {
    int best_idx = -1;
    UBaseType_t best_prio = 0;
    for (int i = 0; i < MAX_TASKS; i++) {
        if (s_tasks[i].state == TASK_WAIT_SEM && s_tasks[i].wait_obj == s) {
            if (best_idx < 0 || s_tasks[i].priority > best_prio) {
                best_idx = i;
                best_prio = s_tasks[i].priority;
            }
        }
    }
    if (best_idx >= 0 && s->count > 0) {
        s->count--;
        if (s->type == SEM_MUTEX || s->type == SEM_RECURSIVE_MUTEX) {
            s->owner_task = best_idx;
            s->recursion = 1;
        }
        s_tasks[best_idx].wait_obj = NULL;
        s_tasks[best_idx].wait_succeeded = true;
        s_tasks[best_idx].state = TASK_READY;
    }
}

BaseType_t xSemaphoreTake(SemaphoreHandle_t xSemaphore, TickType_t xBlockTime) {
    sim_sem_t *s = (sim_sem_t *)xSemaphore;
    if (!s || s->type == SEM_UNUSED) return pdFALSE;
    if (s->type == SEM_RECURSIVE_MUTEX) {
        return xSemaphoreTakeRecursive(xSemaphore, xBlockTime);
    }

    if (s->count > 0) {
        s->count--;
        if (s->type == SEM_MUTEX) {
            s->owner_task = s_current_task;
            s->recursion = 1;
        }
        return pdTRUE;
    }

    if (xBlockTime == 0 || s_current_task < 0) {
        return pdFALSE;
    }

    sim_task_t *me = &s_tasks[s_current_task];
    me->state = TASK_WAIT_SEM;
    me->wait_obj = s;
    me->wake_us = ticks_to_deadline_us(xBlockTime);
    me->wait_succeeded = false;
    yield_to_scheduler();
    return me->wait_succeeded ? pdTRUE : pdFALSE;
}

BaseType_t xSemaphoreTakeRecursive(SemaphoreHandle_t xMutex, TickType_t xBlockTime) {
    sim_sem_t *s = (sim_sem_t *)xMutex;
    if (!s || s->type == SEM_UNUSED) return pdFALSE;

    if (s_current_task >= 0 && s->owner_task == s_current_task) {
        s->recursion++;
        return pdTRUE;
    }
    if (s->count > 0) {
        s->count = 0;
        s->owner_task = s_current_task;
        s->recursion = 1;
        return pdTRUE;
    }
    if (xBlockTime == 0 || s_current_task < 0) {
        return pdFALSE;
    }

    sim_task_t *me = &s_tasks[s_current_task];
    me->state = TASK_WAIT_SEM;
    me->wait_obj = s;
    me->wake_us = ticks_to_deadline_us(xBlockTime);
    me->wait_succeeded = false;
    yield_to_scheduler();
    return me->wait_succeeded ? pdTRUE : pdFALSE;
}

BaseType_t xSemaphoreTakeFromISR(SemaphoreHandle_t xSemaphore, BaseType_t *pxHigherPriorityTaskWoken) {
    if (pxHigherPriorityTaskWoken) *pxHigherPriorityTaskWoken = pdFALSE;
    return xSemaphoreTake(xSemaphore, 0);
}

BaseType_t xSemaphoreGive(SemaphoreHandle_t xSemaphore) {
    sim_sem_t *s = (sim_sem_t *)xSemaphore;
    if (!s || s->type == SEM_UNUSED) return pdFALSE;
    if (s->type == SEM_RECURSIVE_MUTEX) {
        return xSemaphoreGiveRecursive(xSemaphore);
    }

    if (s->type == SEM_MUTEX) {
        s->owner_task = -1;
        s->recursion = 0;
        s->count = 1;
        wake_highest_waiter_for_sem(s);
        return pdTRUE;
    }

    if (s->count >= s->max_count) {
        return pdFALSE;
    }
    s->count++;
    wake_highest_waiter_for_sem(s);
    return pdTRUE;
}

BaseType_t xSemaphoreGiveRecursive(SemaphoreHandle_t xMutex) {
    sim_sem_t *s = (sim_sem_t *)xMutex;
    if (!s || s->type == SEM_UNUSED) return pdFALSE;

    if (s->owner_task != s_current_task && s->owner_task != -1) {
        return pdFALSE;
    }
    if (s->recursion > 1) {
        s->recursion--;
        return pdTRUE;
    }
    s->recursion = 0;
    s->owner_task = -1;
    s->count = 1;
    wake_highest_waiter_for_sem(s);
    return pdTRUE;
}

BaseType_t xSemaphoreGiveFromISR(SemaphoreHandle_t xSemaphore, BaseType_t *pxHigherPriorityTaskWoken) {
    if (pxHigherPriorityTaskWoken) *pxHigherPriorityTaskWoken = pdFALSE;
    return xSemaphoreGive(xSemaphore);
}

UBaseType_t uxSemaphoreGetCount(SemaphoreHandle_t xSemaphore) {
    sim_sem_t *s = (sim_sem_t *)xSemaphore;
    return s ? s->count : 0;
}

/* ============================================================================
 * Queues (Pass-by-Value FIFO)
 * ============================================================================ */

QueueHandle_t xQueueCreate(const UBaseType_t uxQueueLength, const UBaseType_t uxItemSize) {
    if (uxQueueLength == 0) return NULL;
    for (int i = 0; i < MAX_QUEUES; i++) {
        if (!s_queues[i].used) {
            uint8_t *buf = NULL;
            if (uxItemSize > 0) {
                buf = (uint8_t *)calloc(uxQueueLength, uxItemSize);
                if (!buf) return NULL;
            }
            s_queues[i].used = true;
            s_queues[i].persistent = !s_scheduler_initialized;
            s_queues[i].length = uxQueueLength;
            s_queues[i].item_size = uxItemSize;
            s_queues[i].head = 0;
            s_queues[i].tail = 0;
            s_queues[i].count = 0;
            s_queues[i].storage = buf;
            return (QueueHandle_t)&s_queues[i];
        }
    }
    fprintf(stderr, "[freertos_sim] ERROR: Out of queue slots\n");
    return NULL;
}

void vQueueDelete(QueueHandle_t xQueue) {
    sim_queue_t *q = (sim_queue_t *)xQueue;
    if (!q || !q->used || q->persistent) return;
    free(q->storage);
    memset(q, 0, sizeof(*q));
}

static void queue_push_raw(sim_queue_t *q, const void *item, bool front) {
    if (q->item_size > 0 && item) {
        if (front) {
            q->head = (q->head == 0) ? (q->length - 1) : (q->head - 1);
            memcpy(q->storage + (size_t)q->head * q->item_size, item, q->item_size);
        } else {
            memcpy(q->storage + (size_t)q->tail * q->item_size, item, q->item_size);
            q->tail = (q->tail + 1) % q->length;
        }
    } else if (!front) {
        q->tail = (q->tail + 1) % q->length;
    }
    q->count++;
}

static void queue_pop_raw(sim_queue_t *q, void *out) {
    if (q->item_size > 0 && out) {
        memcpy(out, q->storage + (size_t)q->head * q->item_size, q->item_size);
    }
    q->head = (q->head + 1) % q->length;
    q->count--;
}

static void queue_wake_waiters(sim_queue_t *q) {
    /* First, deliver items to waiting receivers */
    while (q->count > 0) {
        int best_rx = -1;
        UBaseType_t best_prio = 0;
        for (int i = 0; i < MAX_TASKS; i++) {
            if (s_tasks[i].state == TASK_WAIT_QUEUE_RECV && s_tasks[i].wait_obj == q) {
                if (best_rx < 0 || s_tasks[i].priority > best_prio) {
                    best_rx = i;
                    best_prio = s_tasks[i].priority;
                }
            }
        }
        if (best_rx < 0) break;
        queue_pop_raw(q, s_tasks[best_rx].queue_recv_buf);
        s_tasks[best_rx].queue_recv_buf = NULL;
        s_tasks[best_rx].wait_obj = NULL;
        s_tasks[best_rx].wait_succeeded = true;
        s_tasks[best_rx].state = TASK_READY;
    }

    /* Next, accept items from waiting senders */
    while (q->count < q->length) {
        int best_tx = -1;
        UBaseType_t best_prio = 0;
        for (int i = 0; i < MAX_TASKS; i++) {
            if (s_tasks[i].state == TASK_WAIT_QUEUE_SEND && s_tasks[i].wait_obj == q) {
                if (best_tx < 0 || s_tasks[i].priority > best_prio) {
                    best_tx = i;
                    best_prio = s_tasks[i].priority;
                }
            }
        }
        if (best_tx < 0) break;
        queue_push_raw(q, s_tasks[best_tx].queue_send_item, s_tasks[best_tx].queue_send_front);
        s_tasks[best_tx].queue_send_item = NULL;
        s_tasks[best_tx].wait_obj = NULL;
        s_tasks[best_tx].wait_succeeded = true;
        s_tasks[best_tx].state = TASK_READY;
    }
}

static BaseType_t queue_send_internal(QueueHandle_t xQueue,
                                      const void *pvItemToQueue,
                                      TickType_t xTicksToWait,
                                      bool front) {
    sim_queue_t *q = (sim_queue_t *)xQueue;
    if (!q || !q->used) return pdFALSE;

    if (q->count < q->length) {
        queue_push_raw(q, pvItemToQueue, front);
        queue_wake_waiters(q);
        return pdTRUE;
    }
    if (xTicksToWait == 0 || s_current_task < 0) {
        return errQUEUE_FULL;
    }

    sim_task_t *me = &s_tasks[s_current_task];
    me->state = TASK_WAIT_QUEUE_SEND;
    me->wait_obj = q;
    me->queue_send_item = pvItemToQueue;
    me->queue_send_front = front;
    me->wake_us = ticks_to_deadline_us(xTicksToWait);
    me->wait_succeeded = false;
    yield_to_scheduler();
    queue_wake_waiters(q);
    return me->wait_succeeded ? pdTRUE : errQUEUE_FULL;
}

BaseType_t xQueueSend(QueueHandle_t xQueue, const void *pvItemToQueue, TickType_t xTicksToWait) {
    return queue_send_internal(xQueue, pvItemToQueue, xTicksToWait, false);
}

BaseType_t xQueueSendToBack(QueueHandle_t xQueue, const void *pvItemToQueue, TickType_t xTicksToWait) {
    return queue_send_internal(xQueue, pvItemToQueue, xTicksToWait, false);
}

BaseType_t xQueueSendToFront(QueueHandle_t xQueue, const void *pvItemToQueue, TickType_t xTicksToWait) {
    return queue_send_internal(xQueue, pvItemToQueue, xTicksToWait, true);
}

BaseType_t xQueueSendFromISR(QueueHandle_t xQueue,
                             const void *pvItemToQueue,
                             BaseType_t *pxHigherPriorityTaskWoken) {
    if (pxHigherPriorityTaskWoken) *pxHigherPriorityTaskWoken = pdFALSE;
    return queue_send_internal(xQueue, pvItemToQueue, 0, false);
}

BaseType_t xQueueOverwrite(QueueHandle_t xQueue, const void *pvItemToQueue) {
    sim_queue_t *q = (sim_queue_t *)xQueue;
    if (!q || !q->used) return pdFALSE;
    q->head = 0;
    q->tail = 0;
    q->count = 0;
    queue_push_raw(q, pvItemToQueue, false);
    queue_wake_waiters(q);
    return pdTRUE;
}

BaseType_t xQueueOverwriteFromISR(QueueHandle_t xQueue,
                                  const void *pvItemToQueue,
                                  BaseType_t *pxHigherPriorityTaskWoken) {
    if (pxHigherPriorityTaskWoken) *pxHigherPriorityTaskWoken = pdFALSE;
    return xQueueOverwrite(xQueue, pvItemToQueue);
}

BaseType_t xQueueReceive(QueueHandle_t xQueue, void *pvBuffer, TickType_t xTicksToWait) {
    sim_queue_t *q = (sim_queue_t *)xQueue;
    if (!q || !q->used) return pdFALSE;

    if (q->count > 0) {
        queue_pop_raw(q, pvBuffer);
        queue_wake_waiters(q);
        return pdTRUE;
    }
    if (xTicksToWait == 0 || s_current_task < 0) {
        return errQUEUE_EMPTY;
    }

    sim_task_t *me = &s_tasks[s_current_task];
    me->state = TASK_WAIT_QUEUE_RECV;
    me->wait_obj = q;
    me->queue_recv_buf = pvBuffer;
    me->wake_us = ticks_to_deadline_us(xTicksToWait);
    me->wait_succeeded = false;
    yield_to_scheduler();
    return me->wait_succeeded ? pdTRUE : errQUEUE_EMPTY;
}

BaseType_t xQueueReceiveFromISR(QueueHandle_t xQueue, void *pvBuffer, BaseType_t *pxHigherPriorityTaskWoken) {
    if (pxHigherPriorityTaskWoken) *pxHigherPriorityTaskWoken = pdFALSE;
    return xQueueReceive(xQueue, pvBuffer, 0);
}

BaseType_t xQueuePeek(QueueHandle_t xQueue, void *pvBuffer, TickType_t xTicksToWait) {
    sim_queue_t *q = (sim_queue_t *)xQueue;
    (void)xTicksToWait;
    if (!q || !q->used || q->count == 0) return pdFALSE;
    if (q->item_size > 0 && pvBuffer) {
        memcpy(pvBuffer, q->storage + (size_t)q->head * q->item_size, q->item_size);
    }
    return pdTRUE;
}

BaseType_t xQueueReset(QueueHandle_t xQueue) {
    sim_queue_t *q = (sim_queue_t *)xQueue;
    if (!q || !q->used) return pdFALSE;
    q->head = 0;
    q->tail = 0;
    q->count = 0;
    return pdPASS;
}

UBaseType_t uxQueueMessagesWaiting(const QueueHandle_t xQueue) {
    const sim_queue_t *q = (const sim_queue_t *)xQueue;
    return (q && q->used) ? q->count : 0;
}

UBaseType_t uxQueueSpacesAvailable(const QueueHandle_t xQueue) {
    const sim_queue_t *q = (const sim_queue_t *)xQueue;
    return (q && q->used) ? (q->length - q->count) : 0;
}

/* ============================================================================
 * Event Groups
 * ============================================================================ */

EventGroupHandle_t xEventGroupCreate(void) {
    for (int i = 0; i < MAX_EVENT_GROUPS; i++) {
        if (!s_event_groups[i].used) {
            s_event_groups[i].used = true;
            s_event_groups[i].persistent = !s_scheduler_initialized;
            s_event_groups[i].bits = 0;
            return (EventGroupHandle_t)&s_event_groups[i];
        }
    }
    return NULL;
}

void vEventGroupDelete(EventGroupHandle_t xEventGroup) {
    sim_event_group_t *eg = (sim_event_group_t *)xEventGroup;
    if (!eg || eg->persistent) return;
    memset(eg, 0, sizeof(*eg));
}

static bool eg_condition_met(EventBits_t current, EventBits_t wait_bits, bool wait_all) {
    if (wait_all) {
        return (current & wait_bits) == wait_bits;
    }
    return (current & wait_bits) != 0;
}

EventBits_t xEventGroupSetBits(EventGroupHandle_t xEventGroup, const EventBits_t uxBitsToSet) {
    sim_event_group_t *eg = (sim_event_group_t *)xEventGroup;
    if (!eg || !eg->used) return 0;

    eg->bits |= uxBitsToSet;
    EventBits_t snapshot = eg->bits;
    EventBits_t to_clear = 0;

    for (int i = 0; i < MAX_TASKS; i++) {
        sim_task_t *t = &s_tasks[i];
        if (t->state == TASK_WAIT_EVENT && t->wait_obj == eg) {
            if (eg_condition_met(snapshot, t->eg_wait_bits, t->eg_wait_all)) {
                t->eg_result_bits = snapshot;
                if (t->eg_clear_on_exit) {
                    to_clear |= t->eg_wait_bits;
                }
                t->wait_obj = NULL;
                t->wait_succeeded = true;
                t->state = TASK_READY;
            }
        }
    }
    eg->bits &= ~to_clear;
    return eg->bits;
}

BaseType_t xEventGroupSetBitsFromISR(EventGroupHandle_t xEventGroup,
                                     const EventBits_t uxBitsToSet,
                                     BaseType_t *pxHigherPriorityTaskWoken) {
    if (pxHigherPriorityTaskWoken) *pxHigherPriorityTaskWoken = pdFALSE;
    xEventGroupSetBits(xEventGroup, uxBitsToSet);
    return pdPASS;
}

EventBits_t xEventGroupClearBits(EventGroupHandle_t xEventGroup, const EventBits_t uxBitsToClear) {
    sim_event_group_t *eg = (sim_event_group_t *)xEventGroup;
    if (!eg || !eg->used) return 0;
    EventBits_t prev = eg->bits;
    eg->bits &= ~uxBitsToClear;
    return prev;
}

EventBits_t xEventGroupGetBits(EventGroupHandle_t xEventGroup) {
    sim_event_group_t *eg = (sim_event_group_t *)xEventGroup;
    return (eg && eg->used) ? eg->bits : 0;
}

EventBits_t xEventGroupWaitBits(EventGroupHandle_t xEventGroup,
                                const EventBits_t uxBitsToWaitFor,
                                const BaseType_t xClearOnExit,
                                const BaseType_t xWaitForAllBits,
                                TickType_t xTicksToWait) {
    sim_event_group_t *eg = (sim_event_group_t *)xEventGroup;
    if (!eg || !eg->used) return 0;

    if (eg_condition_met(eg->bits, uxBitsToWaitFor, xWaitForAllBits != pdFALSE)) {
        EventBits_t ret = eg->bits;
        if (xClearOnExit != pdFALSE) {
            eg->bits &= ~uxBitsToWaitFor;
        }
        return ret;
    }

    if (xTicksToWait == 0 || s_current_task < 0) {
        return eg->bits;
    }

    sim_task_t *me = &s_tasks[s_current_task];
    me->state = TASK_WAIT_EVENT;
    me->wait_obj = eg;
    me->eg_wait_bits = uxBitsToWaitFor;
    me->eg_wait_all = (xWaitForAllBits != pdFALSE);
    me->eg_clear_on_exit = (xClearOnExit != pdFALSE);
    me->eg_result_bits = 0;
    me->wake_us = ticks_to_deadline_us(xTicksToWait);
    me->wait_succeeded = false;
    yield_to_scheduler();

    if (me->wait_succeeded) {
        return me->eg_result_bits;
    }
    return eg->bits;
}

/* ============================================================================
 * FreeRTOS Software Timers
 * ============================================================================ */

TimerHandle_t xTimerCreate(const char *const pcTimerName,
                           const TickType_t xTimerPeriodInTicks,
                           const BaseType_t xAutoReload,
                           void *const pvTimerID,
                           TimerCallbackFunction_t pxCallbackFunction) {
    for (int i = 0; i < MAX_SW_TIMERS; i++) {
        if (!s_sw_timers[i].used) {
            uint64_t period_us = ((uint64_t)xTimerPeriodInTicks * 1000000ULL) / (uint64_t)CONFIG_FREERTOS_HZ;
            if (period_us == 0) period_us = 1000ULL;
            s_sw_timers[i].used = true;
            s_sw_timers[i].active = false;
            s_sw_timers[i].auto_reload = (xAutoReload != pdFALSE);
            snprintf(s_sw_timers[i].name, sizeof(s_sw_timers[i].name), "%s", pcTimerName ? pcTimerName : "sw_tmr");
            s_sw_timers[i].period_us = period_us;
            s_sw_timers[i].next_fire_us = 0;
            s_sw_timers[i].timer_id = pvTimerID;
            s_sw_timers[i].callback = pxCallbackFunction;
            return (TimerHandle_t)&s_sw_timers[i];
        }
    }
    return NULL;
}

BaseType_t xTimerStart(TimerHandle_t xTimer, const TickType_t xTicksToWait) {
    (void)xTicksToWait;
    sim_sw_timer_t *t = (sim_sw_timer_t *)xTimer;
    if (!t || !t->used) return pdFAIL;
    t->active = true;
    t->next_fire_us = s_now_us + t->period_us;
    return pdPASS;
}

BaseType_t xTimerStop(TimerHandle_t xTimer, const TickType_t xTicksToWait) {
    (void)xTicksToWait;
    sim_sw_timer_t *t = (sim_sw_timer_t *)xTimer;
    if (!t || !t->used) return pdFAIL;
    t->active = false;
    return pdPASS;
}

BaseType_t xTimerReset(TimerHandle_t xTimer, const TickType_t xTicksToWait) {
    return xTimerStart(xTimer, xTicksToWait);
}

BaseType_t xTimerChangePeriod(TimerHandle_t xTimer, const TickType_t xNewPeriod, const TickType_t xTicksToWait) {
    (void)xTicksToWait;
    sim_sw_timer_t *t = (sim_sw_timer_t *)xTimer;
    if (!t || !t->used) return pdFAIL;
    uint64_t period_us = ((uint64_t)xNewPeriod * 1000000ULL) / (uint64_t)CONFIG_FREERTOS_HZ;
    if (period_us == 0) period_us = 1000ULL;
    t->period_us = period_us;
    t->active = true;
    t->next_fire_us = s_now_us + period_us;
    return pdPASS;
}

BaseType_t xTimerDelete(TimerHandle_t xTimer, const TickType_t xTicksToWait) {
    (void)xTicksToWait;
    sim_sw_timer_t *t = (sim_sw_timer_t *)xTimer;
    if (!t || !t->used) return pdFAIL;
    memset(t, 0, sizeof(*t));
    return pdPASS;
}

BaseType_t xTimerIsTimerActive(TimerHandle_t xTimer) {
    sim_sw_timer_t *t = (sim_sw_timer_t *)xTimer;
    return (t && t->used && t->active) ? pdTRUE : pdFALSE;
}

void *pvTimerGetTimerID(const TimerHandle_t xTimer) {
    const sim_sw_timer_t *t = (const sim_sw_timer_t *)xTimer;
    return (t && t->used) ? t->timer_id : NULL;
}

/* ============================================================================
 * esp_timer High-Resolution Virtual Timer API
 * ============================================================================ */

esp_err_t esp_timer_init(void) {
    return ESP_OK;
}

esp_err_t esp_timer_create(const esp_timer_create_args_t *create_args, esp_timer_handle_t *out_handle) {
    if (!create_args || !out_handle) return ESP_ERR_INVALID_ARG;
    for (int i = 0; i < MAX_ESP_TIMERS; i++) {
        if (!s_esp_timers[i].used) {
            memset(&s_esp_timers[i], 0, sizeof(s_esp_timers[i]));
            s_esp_timers[i].used = true;
            s_esp_timers[i].active = false;
            s_esp_timers[i].callback = create_args->callback;
            s_esp_timers[i].arg = create_args->arg;
            snprintf(s_esp_timers[i].name, sizeof(s_esp_timers[i].name), "%s",
                     create_args->name ? create_args->name : "esp_tmr");
            *out_handle = &s_esp_timers[i];
            return ESP_OK;
        }
    }
    return ESP_ERR_NO_MEM;
}

esp_err_t esp_timer_start_once(esp_timer_handle_t timer, uint64_t timeout_us) {
    if (!timer || !timer->used) return ESP_ERR_INVALID_ARG;
    if (timer->active) return ESP_ERR_INVALID_STATE;
    timer->periodic = false;
    timer->period_us = timeout_us;
    timer->next_fire_us = s_now_us + timeout_us;
    timer->active = true;
    return ESP_OK;
}

esp_err_t esp_timer_start_periodic(esp_timer_handle_t timer, uint64_t period_us) {
    if (!timer || !timer->used) return ESP_ERR_INVALID_ARG;
    if (timer->active) return ESP_ERR_INVALID_STATE;
    if (period_us == 0) period_us = 1000ULL;
    timer->periodic = true;
    timer->period_us = period_us;
    timer->next_fire_us = s_now_us + period_us;
    timer->active = true;
    return ESP_OK;
}

esp_err_t esp_timer_restart(esp_timer_handle_t timer, uint64_t timeout_us) {
    if (!timer || !timer->used) return ESP_ERR_INVALID_ARG;
    timer->period_us = timeout_us;
    timer->next_fire_us = s_now_us + timeout_us;
    timer->active = true;
    return ESP_OK;
}

esp_err_t esp_timer_stop(esp_timer_handle_t timer) {
    if (!timer || !timer->used) return ESP_ERR_INVALID_ARG;
    if (!timer->active) return ESP_ERR_INVALID_STATE;
    timer->active = false;
    return ESP_OK;
}

esp_err_t esp_timer_delete(esp_timer_handle_t timer) {
    if (!timer || !timer->used) return ESP_ERR_INVALID_ARG;
    if (timer->active) return ESP_ERR_INVALID_STATE;
    memset(timer, 0, sizeof(*timer));
    return ESP_OK;
}

bool esp_timer_is_active(esp_timer_handle_t timer) {
    return timer && timer->used && timer->active;
}

int64_t esp_timer_get_time(void) {
    return (int64_t)s_now_us;
}

void platform_nudge_status_sync_timer(void) {
    for (int i = 0; i < MAX_ESP_TIMERS; i++) {
        if (s_esp_timers[i].used && s_esp_timers[i].active &&
            strcmp(s_esp_timers[i].name, "status_sync") == 0) {
            uint64_t soon = s_now_us + 50000ULL;
            if (s_esp_timers[i].next_fire_us > soon) {
                s_esp_timers[i].next_fire_us = soon;
            }
        }
    }
}
