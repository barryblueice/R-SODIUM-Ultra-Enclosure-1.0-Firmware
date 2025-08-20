#ifndef HDDPC_H
#define HDDPC_H

#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

typedef void (*hddpc_callback_t)(int gpio_num);

void hddpc_task(void* arg);
extern QueueHandle_t hddpc_evt_queue;

void hddpc1_callback(int gpio_num);
void hddpc2_callback(int gpio_num);
void hddpc3_callback(int gpio_num);
void SATA1_callback(int gpio_num);
void SATA2_callback(int gpio_num);
void gpio_register_callback(gpio_num_t gpio_num, hddpc_callback_t callback);

#endif