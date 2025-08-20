#ifndef GPIO_HANDLE_H
#define GPIO_HANDLE_H

#include <stdint.h>

uint8_t restore_gpio_state(uint8_t gpio_num);
uint8_t ext_restore_gpio_state(uint8_t gpio_num);
void restore_state(void);
void gpio_initialized();

#endif