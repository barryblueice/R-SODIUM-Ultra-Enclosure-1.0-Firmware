#ifndef PROCESS_COMMANDER_H
#define PROCESS_COMMANDER_H

#include <stdint.h>

void process_command(uint8_t cmd, const uint8_t *data);
void send_hid_response(uint8_t command, const uint8_t *payload, size_t payload_len);

#endif