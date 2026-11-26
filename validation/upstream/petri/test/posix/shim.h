/* Force-included (-include) when building the library sources for the host
 * run. It leaves the library source untouched but
 *   - perturbs the schedule: every queue send may be preceded by a yield, so
 *     the tasks interleave in many more orders than plain FIFO wake-ups give;
 *   - counts the library's "!!" anomaly reports instead of printing them. */
#ifndef HARNESS_SHIM_H
#define HARNESS_SHIM_H
#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"
BaseType_t harness_send(QueueHandle_t q, const void *item, TickType_t t);
int harness_printf(const char *fmt, ...);
#undef  xQueueSendToBack
#define xQueueSendToBack(q, item, t) harness_send((q), (item), (t))
#define printf harness_printf
#endif
