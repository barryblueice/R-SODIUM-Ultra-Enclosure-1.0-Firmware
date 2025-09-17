idf_component_register(
    SRCS "alive_hid.c" "irq_queue.c" "ota_updater.c" "process_commander.c" "gpio_handle.c" "nvs_handle.c" "main.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES esp_driver_gpio
    REQUIRES nvs_flash
    REQUIRES mbedtls
    REQUIRES app_update
    )
