#include <unistd.h>
#include "gpio_handle.h"
#include "nvs_handle.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "mbedtls/md.h"
#include <string.h>
#include "class/hid/hid_device.h"
#include "esp_system.h"
#include "esp_rom_sys.h"
#include "esp_sleep.h"
#include "tusb.h"
#include "process_commander.h"
#include "esp_rom_gpio.h"
#include "soc/rtc_cntl_reg.h"
#include "alive_hid.h"

static const char *TAG = "R-SODIUM Controller";
#define REPORT_SIZE 64
#define HMAC_KEY    "a0HyIvVM6A6Z7dTPYrAk8s3Mpouh"

RTC_DATA_ATTR int dfu_flag = 0;

void enter_dfu_mode(void)
{

    ESP_LOGW(TAG, "Preparing to enter ROM DFU mode...");
    REG_WRITE(RTC_CNTL_OPTION1_REG, RTC_CNTL_FORCE_DOWNLOAD_BOOT);
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
}

void send_hid_response(uint8_t command, const uint8_t *payload, size_t payload_len) {
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

    char buf[3 * REPORT_SIZE + 1];
    char *p = buf;
    for (int i = 0; i < REPORT_SIZE; i++) {
        p += sprintf(p, "%02X ", report[i]);
    }
    *p = '\0';

        ESP_LOGI(TAG, "Sent response for cmd 0x%02X: %s", command, buf);
    }

void process_command(uint8_t cmd, const uint8_t *data) {
    // uint8_t ota_value = get_nvs_state(0x00,"ota_update");
    // if (ota_value == 0x01) {
    //     ESP_LOGW(TAG, "Starting OTA Updater");
    //     hid_packet_t pkt;
    //     pkt.command = cmd;
    //     pkt.seq = data[0];
    //     pkt.length = (cmd==0x00) ? 4 : 29;
    //     memcpy(pkt.data, data + 1, pkt.length);
    //     xQueueSend(hid_queue, &pkt, 0);
    //     return;
    // }
    ESP_LOGI(TAG, "Original data: %d %02X %02X %02X %02X %02X", cmd, data[0], data[1], data[2], data[3], data[4]);
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
        case 0x0F:
            // 集体返回供电GPIO的状态
            uint8_t payload[4] = {
                gpio_get_level(GPIO_NUM_45), 
                gpio_get_level(GPIO_NUM_34), 
                gpio_get_level(GPIO_NUM_38), 
                gpio_get_level(GPIO_NUM_1)
            };
            send_hid_response(0x00, payload, sizeof(payload));
            break;
        case 0xFD:
            // 应用全GPIO
            restore_state();
            ESP_LOGI(TAG, "Applied all GPIO");
            break;
        case 0xFC:
            // 重置ESP32
            ESP_LOGI(TAG, "ESP32 Reset");
            esp_restart();
            break;
        case 0xFB:
            // dfu_update
            enter_dfu_mode();
            break;
        default:
            // 未知指令
            send_hid_response(data[0], (const uint8_t *)"UNK", 3);
            break;
        }
    }
}