#ifndef QUEUE_H
#define QUEUE_H
#include "FreeRTOS.h"
QueueHandle_t xQueueCreate(UBaseType_t len, UBaseType_t item);
BaseType_t xQueueSendToBack(QueueHandle_t q, const void* item, TickType_t t);
BaseType_t xQueueReceive(QueueHandle_t q, void* buf, TickType_t t);
void vQueueDelete(QueueHandle_t q);   /* C++ RAII Wire deletes its queue */
#endif
