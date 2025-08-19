#ifndef OTA_UPDATER_H
#define OTA_UPDATER_H

#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#define FILE_TIMEOUT_MS 5000

typedef struct {
    uint8_t command;
    uint8_t seq;
    uint8_t data[29];
    size_t length;
} hid_packet_t;


extern QueueHandle_t hid_queue;
extern size_t file_offset;
extern uint8_t expected_crc[4];
extern uint8_t expected_seq;
extern TickType_t last_packet_tick;

void ota_task(void *arg);
void ota_init(void);

#endif
