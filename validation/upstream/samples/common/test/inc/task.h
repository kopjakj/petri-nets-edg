#ifndef TASK_H
#define TASK_H
#include "FreeRTOS.h"
TickType_t xTaskGetTickCount(void);
void vTaskDelayUntil(TickType_t* prev, TickType_t inc);
BaseType_t xTaskCreate(void(*fn)(void*), const char* n, uint32_t s, void* p, UBaseType_t pr, TaskHandle_t* h);
void vTaskStartScheduler(void);
void vTaskDelay(TickType_t);
void vTaskDelete(TaskHandle_t);   /* C++ RAII Object deletes its task */

/* Task-notification stubs (interrupt-driven input path; unused by the tests). */
static inline TaskHandle_t xTaskGetCurrentTaskHandle(void) { return (TaskHandle_t)0; }
static inline void vTaskNotifyGiveFromISR(TaskHandle_t t, BaseType_t* woken) { (void)t; (void)woken; }
static inline uint32_t ulTaskNotifyTake(BaseType_t clear, TickType_t wait) { (void)clear; (void)wait; return 0; }
#endif
