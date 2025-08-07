#include <stdio.h>
#include "nvs_flash.h"
#include "nvs.h"
#include "nvs_handle.h"
#include "esp_log.h"

static const char *TAG = "R-SODIUM Controller";

uint8_t get_nvs_state(uint8_t gpio_num, const char *prefix) {
    nvs_handle_t nvs_handle;
    uint8_t value = 0;
    char key[16];
    snprintf(key, sizeof(key), "%s_%d", prefix, gpio_num);

    esp_err_t ret = nvs_open("storage", NVS_READONLY, &nvs_handle);
    if (ret == ESP_OK) {
        if (nvs_get_u8(nvs_handle, key, &value) != ESP_OK) {
            value = 0;
        }
        nvs_close(nvs_handle);
    }
    return value;
}

void init_nvs() {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }
}

void save_state(uint8_t gpio_num, uint8_t value, const char *prefix) {
    nvs_handle_t nvs_handle;
    char key[16];
    snprintf(key, sizeof(key), "%s_%d", prefix, gpio_num);

    ESP_LOGI(TAG, "Saving value %d for %s_%d", value, prefix, gpio_num);

    if (nvs_open("storage", NVS_READWRITE, &nvs_handle) == ESP_OK) {
        nvs_set_u8(nvs_handle, key, value);
        nvs_commit(nvs_handle);
        nvs_close(nvs_handle);
    }
}


uint8_t enclosure_mode_selected() {
    nvs_handle_t nvs_handle;
    uint8_t value = 0;
    char key[17];
    snprintf(key, sizeof(key), "enclosure_mode_0");

    esp_err_t ret = nvs_open("storage", NVS_READONLY, &nvs_handle);
    if (ret == ESP_OK) {
        if (nvs_get_u8(nvs_handle, key, &value) == ESP_OK) {
            return value;
        } else {
            save_state(0x00, 0, "enclosure_mode");
            return 0x00;
        }
        nvs_close(nvs_handle);
    } else {
        save_state(0x00, 0, "enclosure_mode");
        return 0x00;
    }
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