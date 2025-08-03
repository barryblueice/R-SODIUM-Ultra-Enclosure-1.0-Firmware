#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "tinyusb.h"
#include "mbedtls/md.h"
#include "class/hid/hid_device.h"

static const char *TAG = "R-SODIUM Controller";
#define REPORT_SIZE 64
#define HMAC_KEY    "a0HyIvVM6A6Z7dTPYrAk8s3Mpouh"
// #define LED_GPIO GPIO_NUM_1 // GPIO1
#define LED_GPIO_MASK ((1ULL << GPIO_NUM_1) | (1ULL << GPIO_NUM_2) | (1ULL << GPIO_NUM_3) | (1ULL << GPIO_NUM_4))
#define PWR_GPIO_MASK ((1ULL << GPIO_NUM_5) | (1ULL << GPIO_NUM_6))


// HID 报告描述符
const uint8_t hid_report_descriptor[] = {
    TUD_HID_REPORT_DESC_GENERIC_INOUT(REPORT_SIZE)
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

    ESP_LOGI(TAG, "Sent response for cmd 0x%02X", command);
}

void init_nvs() {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }
}

void save_gpio_state(uint8_t gpio_num, uint8_t value) {
    nvs_handle_t nvs_handle;
    char key[16];
    snprintf(key, sizeof(key), "gpio_%d", gpio_num);

    if (nvs_open("storage", NVS_READWRITE, &nvs_handle) == ESP_OK) {
        nvs_set_u8(nvs_handle, key, value);
        nvs_commit(nvs_handle);
        nvs_close(nvs_handle);
    }
}

void nvs_save(char _type, uint8_t value) {
    nvs_handle_t nvs_handle;
    char key[16];
    snprintf(key, sizeof(key), "%c", _type);

    if (nvs_open("storage", NVS_READWRITE, &nvs_handle) == ESP_OK) {
        nvs_set_u8(nvs_handle, key, value);
        nvs_commit(nvs_handle);
        nvs_close(nvs_handle);
    }
}

void restore_gpio_state(uint8_t gpio_num) {
    nvs_handle_t nvs_handle;
    uint8_t value = 0;
    char key[16];
    snprintf(key, sizeof(key), "gpio_%d", gpio_num);

    esp_err_t ret = nvs_open("storage", NVS_READONLY, &nvs_handle);
    if (ret == ESP_OK) {
        if (nvs_get_u8(nvs_handle, key, &value) == ESP_OK) {
            gpio_set_level(gpio_num, value);
            ESP_LOGI(TAG, "Restored GPIO %d to value: %d", gpio_num, value);
        } else {
            gpio_set_level(gpio_num, 0);
            ESP_LOGW(TAG, "No saved state for GPIO %d, set to 0", gpio_num);
            save_gpio_state(gpio_num, 0); // 确保在 NVS 中保存默认状态
            ESP_LOGI(TAG, "Default state saved for GPIO %d", gpio_num);
        }
        nvs_close(nvs_handle);
    } else {
        gpio_set_level(gpio_num, 0);
        ESP_LOGE(TAG, "NVS open failed (0x%x): GPIO %d set to 0", ret, gpio_num);
        save_gpio_state(gpio_num, 0); // 确保在 NVS 中保存默认状态
        ESP_LOGI(TAG, "Default state saved for GPIO %d", gpio_num);
    }
}

uint8_t get_nvs_gpio_state(uint8_t gpio_num) {
    nvs_handle_t nvs_handle;
    uint8_t value = 0;
    char key[16];
    snprintf(key, sizeof(key), "gpio_%d", gpio_num);

    esp_err_t ret = nvs_open("storage", NVS_READONLY, &nvs_handle);
    if (ret == ESP_OK) {
        if (nvs_get_u8(nvs_handle, key, &value) == ESP_OK) {
            return value;
        } else {
            return 0; // 如果没有保存的状态，返回默认值0
        }
        nvs_close(nvs_handle);
    } else {
        return 0; // 如果 NVS 打开失败，返回默认值0
    }
}

static void process_command(uint8_t cmd, const uint8_t *data) {
    // ESP_LOGI(TAG, "Received cmd 0x%02X", cmd);
    ESP_LOGI(TAG, "Original data: %d %02X %02X", cmd, data[0], data[1]);
    if (cmd == 0xFE) {
        // 处理 PING 命令
        send_hid_response(cmd, (const uint8_t *)"PONG", 4);
        return;
    } else {
        uint8_t gpio_num = (uint8_t)cmd;
        ESP_LOGI(TAG, "Processing GPIO command for GPIO %d, status: %d", gpio_num, data[0]);
        switch (data[0])
        {
        case 0x01:
            gpio_set_level(gpio_num, 1);
            send_hid_response(data[0], (const uint8_t *)"OK", 2);
            if (data[1] == 0x01) {
                save_gpio_state(gpio_num, 1);
            }
            break;
        case 0x00:
            gpio_set_level(gpio_num, 0);
            send_hid_response(data[0], (const uint8_t *)"OK", 2);
            if (data[1] == 0x01) {
                save_gpio_state(gpio_num, 0);
            }
            
            break;
        case 0x02:
            // 读取 输出GPIO 状态
            uint8_t value = get_nvs_gpio_state(gpio_num);
            const char *saved_response = value ? "HIGH" : "LOW";
            send_hid_response(data[0], (const uint8_t *)saved_response, strlen(saved_response));
            break;
        case 0x03:
            // 读取 输入GPIO 状态
            int gpio_level = gpio_get_level(gpio_num);
            ESP_LOGI(TAG, "GPIO %d level: %d", gpio_num, gpio_level);
            const char *response = gpio_level ? "HIGH" : "LOW";
            send_hid_response(data[0], (const uint8_t *)response, strlen(response));
            break;
        default:
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


void clear_nvs_all() {
    esp_err_t err;
    err = nvs_flash_erase();
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "NVS erased successfully.");
    }

    err = nvs_flash_init();
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "NVS re-initialized.");
    }
}

void app_main(void) {
    ESP_LOGI(TAG, "R-SODIUM Ultra SSD Enclosure Controller Start");

    init_nvs();

    gpio_config_t io_conf = {
        .pin_bit_mask = LED_GPIO_MASK,
        .mode = GPIO_MODE_INPUT_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config_t pwr_conf = {
        .pin_bit_mask = PWR_GPIO_MASK,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
    gpio_config(&pwr_conf);
    
    restore_gpio_state(GPIO_NUM_1);
    restore_gpio_state(GPIO_NUM_2);
    restore_gpio_state(GPIO_NUM_3);
    restore_gpio_state(GPIO_NUM_4);

    // clear_nvs_all();

    const tinyusb_config_t tusb_cfg = {
        .device_descriptor = NULL,
        .configuration_descriptor = hid_configuration_descriptor,
        .string_descriptor = NULL,
        .external_phy = false,
    };
    ESP_ERROR_CHECK(tinyusb_driver_install(&tusb_cfg));
    ESP_LOGI(TAG, "Controller initialized");

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}