/* sim shim: freertos/task.h: xTaskCreate spawns a detached pthread that runs the task
 * function once and returns; vTaskDelete(NULL) is that thread reaching the end of its own
 * function, so it is a no-op here rather than a real self-delete. Only chime_task (main.c,
 * BSP_HAS_AUDIO) uses either call. */
#pragma once
#include "FreeRTOS.h"
#include <pthread.h>
#include <stdlib.h>
#include <stdint.h>

typedef void *TaskHandle_t;
typedef void (*TaskFunction_t)(void *);

typedef struct { TaskFunction_t fn; void *arg; } sim_task_ctx_t;

static inline void *sim_task_trampoline(void *p)
{
    sim_task_ctx_t ctx = *(sim_task_ctx_t *)p;
    free(p);
    ctx.fn(ctx.arg);
    return NULL;
}

static inline BaseType_t xTaskCreate(TaskFunction_t fn, const char *name, uint32_t stack,
                                     void *arg, unsigned prio, TaskHandle_t *out)
{
    (void)name; (void)stack; (void)prio;
    sim_task_ctx_t *ctx = malloc(sizeof *ctx);
    ctx->fn = fn;
    ctx->arg = arg;
    pthread_t th;
    pthread_create(&th, NULL, sim_task_trampoline, ctx);
    pthread_detach(th);
    if (out) *out = NULL;
    return pdPASS;
}

static inline void vTaskDelete(TaskHandle_t task) { (void)task; }
