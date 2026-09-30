/* sim shim: freertos/FreeRTOS.h: just enough for chime_task's xTaskCreate/vTaskDelete
 * (main.c, BSP_HAS_AUDIO). See freertos/task.h for the actual shim. */
#pragma once
typedef long BaseType_t;
#define pdPASS 1
