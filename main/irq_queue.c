#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "driver/gpio.h"
#include "irq_queue.h"
#include "gpio_handle.h"
#include "nvs_handle.h"

static const char *TAG = "HDDPC Event";

hddpc_callback_t hddpc_callbacks[GPIO_NUM_MAX];

EventGroupHandle_t hddpc_event_group;

QueueHandle_t hddpc_evt_queue = NULL;

static void IRAM_ATTR hddpc_isr_handler(void* arg) {
    int gpio_num = (int) arg;
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xQueueSendFromISR(hddpc_evt_queue, &gpio_num, &xHigherPriorityTaskWoken);
    if (xHigherPriorityTaskWoken) {
        portYIELD_FROM_ISR();
    }
}

void hddpc_task(void* arg) {
    int gpio_num;
    for (;;) {
        if (xQueueReceive(hddpc_evt_queue, &gpio_num, portMAX_DELAY)) {
            if (hddpc_callbacks[gpio_num]) {
                hddpc_callbacks[gpio_num](gpio_num);
            } else {
                ESP_LOGW(TAG, "No callback for GPIO %d", gpio_num);
            }
        }
    }
}

void gpio_register_callback(gpio_num_t gpio_num, hddpc_callback_t callback) {
    hddpc_callbacks[gpio_num] = callback;
    gpio_isr_handler_add(gpio_num, hddpc_isr_handler, (void*) gpio_num);
}

void hddpc3_callback(int gpio_num) {
    uint8_t _level = gpio_get_level(gpio_num);
    ESP_LOGW(TAG, "HDDPC3 (NVMe | GPIO%d) triggered: %d", gpio_num, _level);
    if (_level == 0) {
        gpio_set_level(GPIO_NUM_45, 0);
        ESP_LOGW(TAG,"NVMe Power Down");
    } else {
        gpio_set_level(GPIO_NUM_45, 1);
        ESP_LOGW(TAG,"NVMe Power UP");
    }
}

void hddpc2_callback(int gpio_num) {
    uint8_t _level = gpio_get_level(gpio_num);
    uint8_t hdd_state;
    if (gpio_get_level(GPIO_NUM_1) == 1) {
        hdd_state = ext_restore_gpio_state(GPIO_NUM_38);
    } else {
        hdd_state = restore_gpio_state(GPIO_NUM_38);
    }
    ESP_LOGW(TAG, "HDDPC2 (SATA2 | M.2 | GPIO%d) triggered: %d", gpio_num, _level);
    if (_level == 0) {
        gpio_set_level(GPIO_NUM_38, 0);
        ESP_LOGW(TAG,"SATA2 (M.2) Power Down");
    } else if (_level == 1) {
        if (hdd_state == 1) {
            gpio_set_level(GPIO_NUM_38, 1);
            ESP_LOGW(TAG,"SATA2 (M.2) Power Up");
        }
    }
}

void hddpc1_callback(int gpio_num) {
    uint8_t _level = gpio_get_level(gpio_num);
    uint8_t hdd_state;
    if (gpio_get_level(GPIO_NUM_1) == 1) {
        hdd_state = ext_restore_gpio_state(GPIO_NUM_34);
    } else {
        hdd_state = restore_gpio_state(GPIO_NUM_34);
    }
    ESP_LOGW(TAG, "HDDPC1 (SATA1 | 2.5 | GPIO%d) triggered: %d", gpio_num, _level);
    if (_level == 0) {
        gpio_set_level(GPIO_NUM_34, 0);
        ESP_LOGW(TAG,"SATA1 (2.5) Power Down");
    } else if (_level == 1) {
        if (hdd_state == 1) {
            gpio_set_level(GPIO_NUM_34, 1);
            ESP_LOGW(TAG,"SATA1 (2.5) Power Up");
        }
    }
    
}

void SATA1_callback(int gpio_num) {
    uint8_t _level = gpio_get_level(gpio_num);
    uint8_t hddpc_state = gpio_get_level(GPIO_NUM_11);
    ESP_LOGW(TAG, "SATA1 Power (2.5 | GPIO%d) triggered: %d", gpio_num, _level);
    if (_level == 1) {
        if (hddpc_state == 1) {
            gpio_set_level(GPIO_NUM_34, 1);
            ESP_LOGW(TAG,"SATA1 (2.5) Power Up");
        }
    }
}

void SATA2_callback(int gpio_num) {
    uint8_t _level = gpio_get_level(gpio_num);
    uint8_t hddpc_state = gpio_get_level(GPIO_NUM_12);
    ESP_LOGW(TAG, "SATA2 Power (M.2 | GPIO%d) triggered: %d", gpio_num, _level);
    if (_level == 1) {
        if (hddpc_state == 1) {
            gpio_set_level(GPIO_NUM_38, 1);
            ESP_LOGW(TAG,"SATA2 (M.2) Power Up");
        }
    }
}

void bus_power_callback(int gpio_num) {
    uint8_t _level = gpio_get_level(gpio_num);
    ESP_LOGW(TAG, "Bus power triggered: %d, ESP32 RESET", _level);
    uint8_t ext_restart_value = get_nvs_state(0x00, "ext_restart");
    if (ext_restart_value == 0x01) {
    // restore_state();
        esp_restart();
    }
    restore_state();
}