#include "class/hid/hid_device.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "esp_log.h"

#define REPORT_SIZE 64

static const char *TAG = "R-SODIUM Controller";

static TaskHandle_t hid_alive_handle = NULL;

void hid_alive_task(void *pvParameters) {
    vTaskDelay(pdMS_TO_TICKS(8000));
    while (1) {
        uint8_t report[REPORT_SIZE];
        memset(report, 0xFF, REPORT_SIZE);
        tud_hid_report(0, report, REPORT_SIZE);
        ESP_LOGD(TAG, "Sent HID report filled with 0xFF");
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

void start_hid_alive_task() {

    ESP_LOGI(TAG,"Start hid alive task...");
    xTaskCreate(hid_alive_task, "hid_alive_task", 4096, NULL, 5, &hid_alive_handle);
}

void stop_hid_alive_task() {
    if (hid_alive_handle != NULL) {
        vTaskDelete(hid_alive_handle);
        hid_alive_handle = NULL;
        ESP_LOGI(TAG,"Stop hid alive task...");
    }
}