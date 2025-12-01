idf_component_register(
    SRCS
        "src/xn_lottie_manager.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        lvgl
        spiffs
        xn_lvgl_driver
        freertos
)

# Create SPIFFS partition image for Lottie animation resources
spiffs_create_partition_image(lottie_spiffs lottie_spiffs FLASH_IN_PROJECT)
