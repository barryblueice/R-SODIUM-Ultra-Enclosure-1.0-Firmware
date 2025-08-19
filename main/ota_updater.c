#include "ota_updater.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_partition.h"
#include "esp_ota_ops.h"
#include "nvs_handle.h"
#include <string.h>
#include "esp_log.h"
#include "esp_crc.h"
#include "esp_heap_caps.h"

uint8_t *file_buffer = NULL;
static const char *TAG = "OTA Updater";
#define MAX_FILE_SIZE   (2*1024*1024)

QueueHandle_t hid_queue;
size_t file_offset = 0;
uint8_t expected_crc[4];
uint8_t expected_seq = 0;
TickType_t last_packet_tick = 0;

uint32_t calc_crc32(const uint8_t *data, size_t length) {
    return esp_crc32_le(0, data, length);
}

void ota_task(void *arg) {
    hid_packet_t pkt;
    for (;;) {
        if (xQueueReceive(hid_queue, &pkt, pdMS_TO_TICKS(FILE_TIMEOUT_MS))) {
            last_packet_tick = xTaskGetTickCount();

            if (pkt.command == 0x00) { // CRC
                memcpy(expected_crc, pkt.data, 4);
                for (int i = 0; i < 4; i++) save_state(i, expected_crc[i], "crc");
                ESP_LOGI(TAG,"Stored CRC32: %02X %02X %02X %02X", expected_crc[0], expected_crc[1], expected_crc[2], expected_crc[3]);
            } else { // 文件片段
                if (pkt.seq != expected_seq) {
                    ESP_LOGW(TAG,"Unexpected seq %d, expected %d, discarding file", pkt.seq, expected_seq);
                    file_offset = 0;
                    expected_seq = 0;
                    continue;
                }

                if (file_offset + pkt.length > MAX_FILE_SIZE) {
                    ESP_LOGE(TAG,"File too big, discarding");
                    file_offset = 0;
                    expected_seq = 0;
                    continue;
                }

                memcpy(file_buffer + file_offset, pkt.data, pkt.length);
                file_offset += pkt.length;
                expected_seq = (expected_seq + 1) & 0xFF;

                if (pkt.command == 0x03) { // 最后一片
                    uint32_t crc_calc = calc_crc32(file_buffer, file_offset);
                    uint32_t crc_expect = (expected_crc[0]<<24)|(expected_crc[1]<<16)|(expected_crc[2]<<8)|expected_crc[3];
                    if (crc_calc == crc_expect) {
                        ESP_LOGI(TAG,"CRC match, starting OTA");
                        const esp_partition_t *update_partition = esp_ota_get_next_update_partition(NULL);
                        esp_partition_erase_range(update_partition, 0, file_offset);
                        esp_partition_write(update_partition, 0, file_buffer, file_offset);
                        esp_ota_set_boot_partition(update_partition);
                        ESP_LOGI(TAG,"OTA complete, restarting...");
                        save_state(0x00, 0x00, "ota_update");
                        esp_restart();
                    } else {
                        ESP_LOGE(TAG,"CRC mismatch, discarding file");
                    }
                    file_offset = 0;
                    expected_seq = 0;
                }
            }
        } else {
            // 超时处理
            if (file_offset > 0) {
                ESP_LOGW(TAG,"File transfer timeout, discarding partial file");
                file_offset = 0;
                expected_seq = 0;
            }
        }
    }
}

void ota_init(void) {
    file_buffer = heap_caps_malloc(MAX_FILE_SIZE, MALLOC_CAP_SPIRAM);
    if (!file_buffer) {
        ESP_LOGE(TAG, "Failed to allocate file buffer");
        return;
    }
    hid_queue = xQueueCreate(20, sizeof(hid_packet_t));
    xTaskCreate(ota_task, "ota_task", 8*1024, NULL, 5, NULL);
}