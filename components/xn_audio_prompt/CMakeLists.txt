idf_component_register(
    SRCS
        "src/audio_prompt.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        xn_audio_manager
        spiffs
)

spiffs_create_partition_image(prompt_spiffs prompt_spiffs FLASH_IN_PROJECT)

