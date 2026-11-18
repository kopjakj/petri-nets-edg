#ifndef PICO_STDLIB_H
#define PICO_STDLIB_H
#include <stdint.h>
#include <stdlib.h>   /* abort (used by the panic stub) */
#define GPIO_IN 0
#define GPIO_OUT 1
void gpio_init(uint32_t);
void gpio_set_dir(uint32_t,int);
void gpio_pull_up(uint32_t);
void gpio_pull_down(uint32_t);
int  gpio_get(uint32_t);
void gpio_put(uint32_t,int);
void stdio_init_all(void);
void sleep_ms(uint32_t);
__attribute__((noreturn)) static inline void panic(const char *fmt, ...) { (void)fmt; abort(); }
#endif
