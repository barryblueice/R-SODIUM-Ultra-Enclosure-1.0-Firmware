#ifndef NVS_HANDLE_H
#define NVS_HANDLE_H

#include <stdint.h>

uint8_t get_nvs_state(uint8_t gpio_num, const char *prefix);
void init_nvs();
void save_state(uint8_t gpio_num, uint8_t value, const char *prefix);
uint8_t enclosure_mode_selected();

#endif