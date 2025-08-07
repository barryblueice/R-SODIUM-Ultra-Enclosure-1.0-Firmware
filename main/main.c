#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "tinyusb.h"
#include "mbedtls/md.h"
#include "class/hid/hid_device.h"
#include <unistd.h>
#include "esp_sleep.h"

#include "nvs_handle.h"
#include "gpio_handle.h"

static const char *TAG = "R-SODIUM Controller";
#define REPORT_SIZE 64
#define HMAC_KEY    "a0HyIvVM6A6Z7dTPYrAk8s3Mpouh"
#define SWITCH_GPIO_MASK ((1ULL << GPIO_NUM_21) | (1ULL << GPIO_NUM_33) | (1ULL << GPIO_NUM_34) | (1ULL << GPIO_NUM_35) | (1ULL << GPIO_NUM_36)  | (1ULL << GPIO_NUM_37)  | (1ULL << GPIO_NUM_38))
#define PWR_GPIO_MASK ((1ULL << GPIO_NUM_1))
#define IO_GPIO_MASK ((1ULL << GPIO_NUM_19))

static volatile bool gpio_int_flag = false;
const uint8_t hid_report_descriptor[] = {
    TUD_HID_REPORT_DESC_GENERIC_INOUT(REPORT_SIZE)
};

static TaskHandle_t hid_alive_handle = NULL;

const char* hid_string_descriptor[5] = {
    (char[]){0x09, 0x04},
    "R-SODIUM Technology",
    "Ultra SSD Enclosure Controller",
    "0D00072100000000",
    "R-SODIUM HID Controller",
};

// 配置描述符
#define TUSB_DESC_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_HID_DESC_LEN)
static const uint8_t hid_configuration_descriptor[] = {
    TUD_CONFIG_DESCRIPTOR(1, 1, 0, TUSB_DESC_TOTAL_LEN, 0x00, 100),
    TUD_HID_DESCRIPTOR(0, 0, false, sizeof(hid_report_descriptor), 0x81, REPORT_SIZE, 10),
};

// HID 描述符回调
uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance) {
    return hid_report_descriptor;
}

// 主机读取 HID 报告（不使用）
uint16_t tud_hid_get_report_cb(uint8_t instance,
                                uint8_t report_id,
                                hid_report_type_t report_type,
                                uint8_t *buffer,

                                uint16_t reqlen) {
    return 0;
}

static void send_hid_response(uint8_t command, const uint8_t *payload, size_t payload_len) {
    uint8_t report[REPORT_SIZE] = {0};
    uint8_t mac[32];

    report[0] = command;
    memcpy(report + 1, payload, payload_len > 31 ? 31 : payload_len);

    mbedtls_md_context_t ctx;
    const mbedtls_md_info_t *info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    mbedtls_md_init(&ctx);
    mbedtls_md_setup(&ctx, info, 1);
    mbedtls_md_hmac_starts(&ctx, (const unsigned char *)HMAC_KEY, strlen(HMAC_KEY));
    mbedtls_md_hmac_update(&ctx, report, 32); // command + payload(31)
    mbedtls_md_hmac_finish(&ctx, mac);
    mbedtls_md_free(&ctx);

    memcpy(report + 32, mac, 32);
    tud_hid_report(0, report, REPORT_SIZE);

    ESP_LOGD(TAG, "Sent response for cmd 0x%02X", command);
}

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

static void process_command(uint8_t cmd, const uint8_t *data) {
    // ESP_LOGI(TAG, "Received cmd 0x%02X", cmd);
    ESP_LOGI(TAG, "Original data: %d %02X %02X %02X %02X %02X", cmd, data[0], data[1], data[2], data[3], data[4]);
    stop_hid_alive_task();
    if (cmd == 0xFE) {
        // 处理 PING 命令
        send_hid_response(cmd, (const uint8_t *)"PONG", 4);
        return;
    } else {
        switch (data[0])
        {
        case 0x01:
            if (data[4] == 0x01) {
                gpio_set_level(cmd, 1);
            }
            send_hid_response(data[0], (const uint8_t *)"OK", 2);
            if (data[1] == 0x01) {
                save_state(cmd, 1, "gpio");
                // ESP_LOGI(TAG, "NVS save %d: 1", cmd);
            }
            break;
        case 0x00:
            if (data[4] == 0x01) {
                gpio_set_level(cmd, 0);
            }
            send_hid_response(data[0], (const uint8_t *)"OK", 2);
            if (data[1] == 0x01) {
                save_state(cmd, 0, "gpio");
                // ESP_LOGI(TAG, "NVS save %d: 0", cmd);
            }
            break;
        case 0x02:
            // 查询NVS存储的GPIO状态
            uint8_t value = get_nvs_state(cmd, "gpio");
            const char *saved_response = value ? "HIGH" : "LOW";
            send_hid_response(data[0], (const uint8_t *)saved_response, strlen(saved_response));
            break;
        case 0x03:
            // 查询当前的GPIO状态
            int gpio_level = gpio_get_level(cmd);
            ESP_LOGI(TAG, "GPIO %d level: %d", cmd, gpio_level);
            const char *response = gpio_level ? "HIGH" : "LOW";
            send_hid_response(data[0], (const uint8_t *)response, strlen(response));
            break;
        case 0x04:
            // 查询NVS存储的硬盘盒状态
            uint8_t enclosure_status = enclosure_mode_selected(data[2]);
            send_hid_response(data[0], &enclosure_status, 1);
            break;
        case 0x05:
            // 硬盘盒模式存储
            save_state(0x00, cmd, "enclosure_mode");
            send_hid_response(data[0], (const uint8_t *)"OK", 2);
            break;
        case 0x06:
            // 存储高电平时的GPIO状态
            save_state(cmd, data[3], "ext_gpio");
            send_hid_response(data[0], (const uint8_t *)"OK", 2);
            break;
        case 0x07:
            // 查询NVS存储的EXT GPIO状态
            uint8_t ext_value = get_nvs_state(cmd, "ext_gpio");
            const char *ext_saved_response = ext_value ? "HIGH" : "LOW";
            send_hid_response(data[0], (const uint8_t *)ext_saved_response, strlen(ext_saved_response));
            break;
        case 0x08:
            // 存储SATA上电时间
            save_state(0x00, cmd, "sata_onpower");
            send_hid_response(data[0], (const uint8_t *)"OK", 2);
            break;
        case 0x09:
            // 向主机端返回SATA上电时间
            uint8_t sata_onpower_time = get_nvs_state(cmd, "sata_onpower");
            send_hid_response(data[0], &sata_onpower_time, 1);
            break;
        case 0x0A:
            // 是否开启主机端休眠
            save_state(0x00, cmd, "susp_en");
            send_hid_response(data[0], (const uint8_t *)"OK", 2);
            break;
        case 0x0B:
            // 向主机端返回主机端休眠状态
            uint8_t suspend_value = get_nvs_state(cmd, "susp_en");
            const char *suspend_enable = suspend_value ? "HIGH" : "LOW";
            send_hid_response(data[0], (const uint8_t *)suspend_enable, strlen(suspend_enable));
            break;
        case 0x0C:
            // 是否开启卸载休眠
            save_state(0x00, cmd, "ususp_en");
            send_hid_response(data[0], (const uint8_t *)"OK", 2);
            break;
        case 0x0D:
            // 向主机端返回卸载休眠状态
            uint8_t umounted_suspend_value = get_nvs_state(0x00, "ususp_en");
            const char *umounted_suspend_enable = umounted_suspend_value ? "HIGH" : "LOW";
            send_hid_response(data[0], (const uint8_t *)umounted_suspend_enable, strlen(umounted_suspend_enable));
            break;
        case 0xFD:
            // 应用全GPIO
            restore_state();
            ESP_LOGI(TAG, "Applied all GPIO");
            break;
        default:
            // 未知指令
            send_hid_response(data[0], (const uint8_t *)"UNK", 3);
            break;
        }
    }
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

    // 计算并验证HMAC
    uint8_t calc_hmac[32];
    mbedtls_md_context_t ctx;
    const mbedtls_md_info_t *info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    mbedtls_md_init(&ctx);
    mbedtls_md_setup(&ctx, info, 1);
    mbedtls_md_hmac_starts(&ctx, (const unsigned char *)HMAC_KEY, strlen(HMAC_KEY));
    mbedtls_md_hmac_update(&ctx, buffer, 32); // command + payload(31)
    mbedtls_md_hmac_finish(&ctx, calc_hmac);
    mbedtls_md_free(&ctx);

    if (memcmp(calc_hmac, recv_hmac, 32) != 0) {
        ESP_LOGW(TAG, "HMAC mismatch");
        return;
    }
    ESP_LOGI(TAG, "Received command: 0x%02X", command);
    process_command(command, payload);

}

void tud_suspend_cb(bool remote_wakeup_en) {
    uint8_t suspend_enable = get_nvs_state(0x00, "susp_en");
    if (suspend_enable != 0x00) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        gpio_set_level(GPIO_NUM_33,0);
        gpio_set_level(GPIO_NUM_34,0);
        gpio_set_level(GPIO_NUM_35,0);
        gpio_set_level(GPIO_NUM_38,0);
        ESP_LOGW(TAG, "Host suspended, disable all GPIO");
        esp_sleep_enable_timer_wakeup(10000000);
        esp_sleep_enable_ext0_wakeup(GPIO_NUM_19, 1);
        esp_light_sleep_start();
        stop_hid_alive_task();
    }
}

void tud_resume_cb(void) {

    restore_state();
    ESP_LOGW(TAG, "Host resumed, restore GPIO state");
    start_hid_alive_task();

}

void tud_mount_cb(void) {

    restore_state();
    ESP_LOGW(TAG, "Host mounted, restore GPIO state");
    start_hid_alive_task();

}

void tud_umount_cb(void) {
    uint8_t suspend_enable = get_nvs_state(0x00, "ususp_en");
    if (suspend_enable != 0x00) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        gpio_set_level(GPIO_NUM_33,0);
        gpio_set_level(GPIO_NUM_34,0);
        gpio_set_level(GPIO_NUM_35,0);
        gpio_set_level(GPIO_NUM_38,0);
        ESP_LOGW(TAG, "Host unmounted, disable all GPIO");
        esp_sleep_enable_timer_wakeup(10000000);
        esp_sleep_enable_ext0_wakeup(GPIO_NUM_19, 1);
        esp_light_sleep_start();
        stop_hid_alive_task();
    }
}

void app_main(void) {
    ESP_LOGI(TAG, "R-SODIUM Ultra SSD Enclosure Controller Start");

    init_nvs();

    gpio_config_t switch_conf = {
        .pin_bit_mask = SWITCH_GPIO_MASK,
        .mode = GPIO_MODE_INPUT_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&switch_conf);

    gpio_config_t pwr_conf = {
        .pin_bit_mask = PWR_GPIO_MASK,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&pwr_conf);

    gpio_config_t io_conf = {
        .pin_bit_mask = IO_GPIO_MASK,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);

    gpio_set_level(GPIO_NUM_21, 1);
    gpio_set_level(GPIO_NUM_33, 0);
    gpio_set_level(GPIO_NUM_34, 0);
    gpio_set_level(GPIO_NUM_35, 0);
    gpio_set_level(GPIO_NUM_38, 0);

    restore_state();

    // clear_nvs_all();

    const tinyusb_config_t tusb_cfg = {
        .device_descriptor = NULL,
        .string_descriptor = hid_string_descriptor,
        .string_descriptor_count = sizeof(hid_string_descriptor) / sizeof(hid_string_descriptor[0]),
        .configuration_descriptor = hid_configuration_descriptor,
        .external_phy = false,
    };
    ESP_ERROR_CHECK(tinyusb_driver_install(&tusb_cfg));
    ESP_LOGI(TAG, "Controller initialized");

    start_hid_alive_task();

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}