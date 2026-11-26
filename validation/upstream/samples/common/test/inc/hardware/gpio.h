#ifndef HARDWARE_GPIO_H
#define HARDWARE_GPIO_H
/* Host test stub: the interrupt-related GPIO symbols edg.c uses that are not
 * already declared in pico/stdlib.h. The gate logic under test never calls the
 * interrupt-driven input path, so these are no-ops. */
#include <stdint.h>
#include <stdbool.h>

typedef unsigned int uint;

#define NUM_BANK0_GPIOS     30
#define GPIO_IRQ_EDGE_RISE  0x8u
#define GPIO_IRQ_EDGE_FALL  0x4u

typedef void (*gpio_irq_callback_t)(uint gpio, uint32_t events);

static inline void gpio_set_irq_enabled(uint gpio, uint32_t events, bool enabled)
{
    (void)gpio; (void)events; (void)enabled;
}
static inline void gpio_set_irq_enabled_with_callback(uint gpio, uint32_t events,
                                                      bool enabled,
                                                      gpio_irq_callback_t cb)
{
    (void)gpio; (void)events; (void)enabled; (void)cb;
}
#endif
