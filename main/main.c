#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include "tusb.h"
#include <psa/crypto.h>
#include "class/hid/hid_device.h"
#include <unistd.h>
#include "esp_sleep.h"

#include "nvs_handle.h"
#include "gpio_handle.h"
#include "process_commander.h"
#include "irq_queue.h"
#include "alive_hid.h"

static volatile bool usb_reenum_req = false;
static volatile bool usb_mounted = false;
static volatile bool usb_init_ready = false;

static const char *TAG = "R-SODIUM Controller";
#define REPORT_SIZE 64
#define HMAC_KEY    "a0HyIvVM6A6Z7dTPYrAk8s3Mpouh"
#define ESP_INTR_FLAG_DEFAULT 0

static volatile bool gpio_int_flag = false;
const uint8_t hid_report_descriptor[] = {
    TUD_HID_REPORT_DESC_GENERIC_INOUT(REPORT_SIZE)
};

const tusb_desc_device_t hid_device_descriptor = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = 0x0200,
    .bDeviceClass       = 0x00,
    .bDeviceSubClass    = 0x00,
    .bDeviceProtocol    = 0x00,
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor           = 0x0D00,
    .idProduct          = 0x0721,
    .bcdDevice          = 0x0100,
    .iManufacturer      = 0x01,
    .iProduct           = 0x02,
    .iSerialNumber      = 0x03,
    .bNumConfigurations = 0x01
};

const char* hid_string_descriptor[5] = {
    (char[]){0x09, 0x04},
    "R-SODIUM Technology",
    "Ultra SSD Enclosure Controller",
    "0D00072100000000",
    "R-SODIUM HID Controller",
};

#define TUSB_DESC_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_HID_DESC_LEN)
static const uint8_t hid_configuration_descriptor[] = {
    TUD_CONFIG_DESCRIPTOR(1, 1, 0, TUSB_DESC_TOTAL_LEN, 0x00, 100),
    TUD_HID_DESCRIPTOR(0, 0, false, sizeof(hid_report_descriptor), 0x81, REPORT_SIZE, 10),
};

uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance) {
    return hid_report_descriptor;
}

uint16_t tud_hid_get_report_cb(uint8_t instance,
                                uint8_t report_id,
                                hid_report_type_t report_type,
                                uint8_t *buffer,

                                uint16_t reqlen) {
    return 0;
}

void tud_hid_set_report_cb(uint8_t instance,
                           uint8_t report_id,
                           hid_report_type_t report_type,
                           uint8_t const *buffer,
                           uint16_t bufsize) {
    if (bufsize != REPORT_SIZE) {
        ESP_LOGW(TAG, "Invalid report size");
        return;
    }

    uint8_t command = buffer[0];
    const uint8_t *payload = buffer + 1;
    const uint8_t *recv_hmac = buffer + 32;
    uint8_t calc_hmac[32];
    size_t calc_hmac_len;

    psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
    psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_SIGN_MESSAGE);
    psa_set_key_algorithm(&attributes, PSA_ALG_HMAC(PSA_ALG_SHA_256));
    psa_set_key_type(&attributes, PSA_KEY_TYPE_HMAC);

    psa_key_id_t hmac_key_id;
    psa_import_key(&attributes, (const uint8_t *)HMAC_KEY, strlen(HMAC_KEY), &hmac_key_id);
    psa_mac_compute(hmac_key_id, PSA_ALG_HMAC(PSA_ALG_SHA_256),
                    buffer, 32, calc_hmac, sizeof(calc_hmac), &calc_hmac_len);
    psa_destroy_key(hmac_key_id);

    if (memcmp(calc_hmac, recv_hmac, 32) != 0) {
        ESP_LOGW(TAG, "HMAC mismatch");
        return;
    }
    ESP_LOGI(TAG, "Received command: 0x%02X", command);
    stop_hid_alive_task();
    process_command(command, payload);

}

void tud_resume_cb(void) {

    restore_state();
    ESP_LOGW(TAG, "Host resumed, restore GPIO state");
    start_hid_alive_task();

}

static void detached_sleep_task(void *param)
{
    vTaskDelay(pdMS_TO_TICKS(5000));
    gpio_set_level(GPIO_NUM_33, 0);
    gpio_set_level(GPIO_NUM_34, 0);
    gpio_set_level(GPIO_NUM_35, 0);
    gpio_set_level(GPIO_NUM_38, 0);
    gpio_set_level(GPIO_NUM_45, 0);
    ESP_LOGW(TAG, "Host unmounted, disable all GPIO");
    esp_sleep_enable_timer_wakeup(10000000);
    esp_light_sleep_start();
    vTaskDelete(NULL);
}

static void device_event_handler(tinyusb_event_t *event, void *arg)
  {
    switch (event->id) {
    case TINYUSB_EVENT_ATTACHED:
        restore_state();
        ESP_LOGW(TAG, "Host mounted, restore GPIO state");
        start_hid_alive_task();
        usb_mounted = true;
        break;
    case TINYUSB_EVENT_DETACHED:
        stop_hid_alive_task();
        usb_mounted = false;
        uint8_t suspend_enable = get_nvs_state(0x00, "ususp_en");
        if (suspend_enable != 0x00) {
            xTaskCreate(detached_sleep_task, "detached_sleep", 2048, NULL, 3, NULL);
        }
        break;
    default:
        break;
    }
  }

// void tud_mount_cb(void) {

//     restore_state();
//     ESP_LOGW(TAG, "Host mounted, restore GPIO state");
//     start_hid_alive_task();
//     usb_mounted = true;

// }

// void tud_umount_cb(void) {
//     uint8_t suspend_enable = get_nvs_state(0x00, "ususp_en");
//     if (suspend_enable != 0x00) {
//         vTaskDelay(pdMS_TO_TICKS(5000));
//         gpio_set_level(GPIO_NUM_33,0);
//         gpio_set_level(GPIO_NUM_34,0);
//         gpio_set_level(GPIO_NUM_35,0);
//         gpio_set_level(GPIO_NUM_38,0);
//         gpio_set_level(GPIO_NUM_45,0);
//         ESP_LOGW(TAG, "Host unmounted, disable all GPIO");
//         esp_sleep_enable_timer_wakeup(10000000);
//         esp_light_sleep_start();
//         stop_hid_alive_task();
//     }
//     usb_mounted = false;
// }

void tud_reset_cb(void)
{
    ESP_LOGW(TAG, "USB bus reset detected");
    restore_state();
    start_hid_alive_task();
    usb_reenum_req = true;
}

void rst_hid_task(void *param)
{
    while (!usb_init_ready) {
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    vTaskDelay(pdMS_TO_TICKS(500));

    ESP_LOGI(TAG, "Waiting for USB to mount...");

    int timeout_ms = 8000;
    int elapsed = 0;
    const int step = 100;

    while (elapsed < timeout_ms) {
        if (tud_mounted()) {
            ESP_LOGI(TAG, "USB mounted successfully, no need to re-enum");
            vTaskDelete(NULL);
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(step));
        elapsed += step;
    }

    ESP_LOGW(TAG, "USB not mounted in %d ms, forcing re-enumeration...", timeout_ms);
    stop_hid_alive_task();
    tud_disconnect();
    vTaskDelay(pdMS_TO_TICKS(1000));
    tud_connect();
    vTaskDelay(pdMS_TO_TICKS(500));
    start_hid_alive_task();
    ESP_LOGI(TAG, "USB re-enumeration complete");

    vTaskDelete(NULL);
}

void usb_task(void *param) {
    while (1) {
        tud_task();
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

void app_main(void) {
    ESP_LOGI(TAG, "R-SODIUM Ultra SSD Enclosure Controller Start");

    psa_crypto_init();

    init_nvs();
    gpio_initialized();

    gpio_set_level(GPIO_NUM_21, 1);
    gpio_set_level(GPIO_NUM_33, 0);
    gpio_set_level(GPIO_NUM_34, 0);
    gpio_set_level(GPIO_NUM_35, 0);
    gpio_set_level(GPIO_NUM_38, 0);
    gpio_set_level(GPIO_NUM_45, 0);

    restore_state();

    // const tinyusb_config_t tusb_cfg = {
    //     .device_descriptor = &hid_device_descriptor,
    //     .string_descriptor = hid_string_descriptor,
    //     .string_descriptor_count = sizeof(hid_string_descriptor) / sizeof(hid_string_descriptor[0]),
    //     .configuration_descriptor = hid_configuration_descriptor,
    //     .external_phy = false,
    //     .self_powered = true,
    //     .vbus_monitor_io = GPIO_NUM_9,
    // };

    tinyusb_config_t tusb_cfg = TINYUSB_DEFAULT_CONFIG(device_event_handler);
    tusb_cfg.descriptor.device = &hid_device_descriptor;
    tusb_cfg.descriptor.string = hid_string_descriptor;
    tusb_cfg.descriptor.string_count = sizeof(hid_string_descriptor)/sizeof(hid_string_descriptor[0]);
    tusb_cfg.descriptor.full_speed_config = hid_configuration_descriptor;
    tusb_cfg.phy.self_powered = true;
    tusb_cfg.phy.vbus_monitor_io = GPIO_NUM_9;

    ESP_ERROR_CHECK(tinyusb_driver_install(&tusb_cfg));
    ESP_LOGI(TAG, "Controller initialized");

    vTaskDelay(pdMS_TO_TICKS(300));
    tud_connect();

    start_hid_alive_task();

    hddpc_evt_queue = xQueueCreate(10, sizeof(int));

    xTaskCreate(hddpc_task, "hddpc_task", 2048, NULL, 5, NULL);
    xTaskCreate(rst_hid_task, "rst_hid_task", 4096, NULL, 6, NULL);

    gpio_install_isr_service(ESP_INTR_FLAG_DEFAULT);

    gpio_register_callback(GPIO_NUM_13, hddpc1_callback);
    gpio_register_callback(GPIO_NUM_12, hddpc2_callback);
    gpio_register_callback(GPIO_NUM_11, hddpc3_callback);
    gpio_register_callback(GPIO_NUM_34, SATA1_callback);
    gpio_register_callback(GPIO_NUM_38, SATA2_callback);
    gpio_register_callback(GPIO_NUM_1, bus_power_callback);

    gpio_set_level(GPIO_NUM_14, 1);

    usb_init_ready = true;
    ESP_LOGI(TAG, "USB init ready, rst_hid_task armed");
}