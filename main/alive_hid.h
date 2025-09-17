#ifndef ALIVE_HID_H
#define ALIVE_HID_H

#include <stdint.h>

void hid_alive_task(void *pvParameters);
void start_hid_alive_task();
void stop_hid_alive_task();

#endif