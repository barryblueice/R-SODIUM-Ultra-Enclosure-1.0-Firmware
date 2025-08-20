#include "driver/gpio.h"
#include "gpio_handle.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "nvs_handle.h"
#include <unistd.h>

#define SWITCH_GPIO_MASK ((1ULL << GPIO_NUM_14) | (1ULL << GPIO_NUM_21) | (1ULL << GPIO_NUM_33) | (1ULL << GPIO_NUM_34) | (1ULL << GPIO_NUM_35) | (1ULL << GPIO_NUM_36)  | (1ULL << GPIO_NUM_37)  | (1ULL << GPIO_NUM_38)  | (1ULL << GPIO_NUM_45))
#define HDDPC_GPIO_MASK ((1ULL << GPIO_NUM_11) | (1ULL << GPIO_NUM_12) | (1ULL << GPIO_NUM_13))
#define PWR_GPIO_MASK ((1ULL << GPIO_NUM_1))
#define IO_GPIO_MASK ((1ULL << GPIO_NUM_19))

static const char *TAG = "GPIO Handler";

uint8_t restore_gpio_state(uint8_t gpio_num) {
    nvs_handle_t nvs_handle;
    uint8_t value = 0;
    char key[16];
    snprintf(key, sizeof(key), "gpio_%d", gpio_num);

    esp_err_t ret = nvs_open("storage", NVS_READONLY, &nvs_handle);
    if (ret == ESP_OK) {
        if (nvs_get_u8(nvs_handle, key, &value) == ESP_OK) {
            if (value == 1) {
                if (gpio_num == 0x22 || gpio_num == 0x26) {
                    uint8_t sata_onpower = get_nvs_state(0x00, "sata_onpower");
                    ESP_LOGI(TAG, "Waiting %d second/s for GPIO %d power-up", sata_onpower, gpio_num);
                    sleep(sata_onpower);
                }
            }
            gpio_set_level(gpio_num, value);
            ESP_LOGI(TAG, "Restored GPIO %d to value: %d", gpio_num, value);
            return value;
        } else {
            gpio_set_level(gpio_num, 0);
            ESP_LOGW(TAG, "No saved state for GPIO %d, set to 0", gpio_num);
            save_state(gpio_num, 0, "gpio"); // 确保在 NVS 中保存默认状态
            ESP_LOGI(TAG, "Default state saved for GPIO %d", gpio_num);
            return 0;
        }
        nvs_close(nvs_handle);
    } else {
        gpio_set_level(gpio_num, 0);
        ESP_LOGE(TAG, "NVS open failed (0x%x): GPIO %d set to 0", ret, gpio_num);
        save_state(gpio_num, 0, "gpio"); // 确保在 NVS 中保存默认状态
        ESP_LOGI(TAG, "Default state saved for GPIO %d", gpio_num);
        return 0;
    }
}

uint8_t ext_restore_gpio_state(uint8_t gpio_num) {
    nvs_handle_t nvs_handle;
    uint8_t value = 0;
    char key[16];
    snprintf(key, sizeof(key), "ext_gpio_%d", gpio_num);

    esp_err_t ret = nvs_open("storage", NVS_READONLY, &nvs_handle);
    if (ret == ESP_OK) {
        if (nvs_get_u8(nvs_handle, key, &value) == ESP_OK) {
            if (value == 1) {
                if (gpio_num == 0x22 || gpio_num == 0x26) {
                    uint8_t sata_onpower = get_nvs_state(0x00, "sata_onpower");
                    ESP_LOGI(TAG, "Waiting %d second/s for EXT-GPIO %d power-up", sata_onpower, gpio_num);
                    sleep(sata_onpower);
                }
            }
            gpio_set_level(gpio_num, value);
            ESP_LOGI(TAG, "Restored GPIO %d to value when ext-powered: %d", gpio_num, value);
            return value;
        } else {
            gpio_set_level(gpio_num, 0);
            ESP_LOGW(TAG, "No saved state for GPIO when ext-powered: %d, set to 0", gpio_num);
            save_state(gpio_num, 0, "gpio"); // 确保在 NVS 中保存默认状态
            ESP_LOGI(TAG, "Default state saved for GPIO when ext-powered: %d", gpio_num);
            return 0;
        }
        nvs_close(nvs_handle);
    } else {
        gpio_set_level(gpio_num, 0);
        ESP_LOGE(TAG, "NVS open failed (0x%x): GPIO when ext-powered %d set to 0", ret, gpio_num);
        save_state(gpio_num, 0, "gpio"); // 确保在 NVS 中保存默认状态
        ESP_LOGI(TAG, "Default state saved for GPIO when ext-powered: %d", gpio_num);
        return 0;
    }
}

void restore_state(void) {
    uint8_t enclosure_state = enclosure_mode_selected();

    if (enclosure_state == 0x00) {
        if (gpio_get_level(GPIO_NUM_1) == 1) {
            ext_restore_gpio_state(GPIO_NUM_33);
            ext_restore_gpio_state(GPIO_NUM_34);
            ext_restore_gpio_state(GPIO_NUM_35);
            ext_restore_gpio_state(GPIO_NUM_38);
            ext_restore_gpio_state(GPIO_NUM_45);
        } else {
            restore_gpio_state(GPIO_NUM_33);
            restore_gpio_state(GPIO_NUM_34);
            restore_gpio_state(GPIO_NUM_35);
            restore_gpio_state(GPIO_NUM_38);
            restore_gpio_state(GPIO_NUM_45);
        }
    } else {
        restore_gpio_state(GPIO_NUM_33);
        restore_gpio_state(GPIO_NUM_34);
        restore_gpio_state(GPIO_NUM_35);
        restore_gpio_state(GPIO_NUM_38);
        restore_gpio_state(GPIO_NUM_45);
    }
    restore_gpio_state(GPIO_NUM_36);
    restore_gpio_state(GPIO_NUM_37);
}

void gpio_initialized() {
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
        .intr_type = GPIO_INTR_ANYEDGE,
    };
    gpio_config(&pwr_conf);

    gpio_config_t hddpc_conf = {
        .pin_bit_mask = HDDPC_GPIO_MASK,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_ANYEDGE,
    };
    gpio_config(&hddpc_conf);

    gpio_config_t io_conf = {
        .pin_bit_mask = IO_GPIO_MASK,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
}