#ifndef FREERTOS_H
#define FREERTOS_H
#include <stdint.h>
typedef uint32_t UBaseType_t;
typedef int32_t  BaseType_t;
typedef uint32_t TickType_t;
typedef void*    QueueHandle_t;
typedef void*    TaskHandle_t;
#define portMAX_DELAY   ((TickType_t)0xffffffff)
#define pdMS_TO_TICKS(x) ((TickType_t)(x))
#define tskIDLE_PRIORITY 0
#define pdTRUE  ((BaseType_t)1)
#define pdFALSE ((BaseType_t)0)
#define pdPASS  ((BaseType_t)1)
#define portYIELD_FROM_ISR(x) do { (void)(x); } while (0)
#endif
