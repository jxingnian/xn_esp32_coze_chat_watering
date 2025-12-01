idf_component_register(
    SRCS 
        "coze_chat.cpp"
        "coze_websocket.cpp"
        "coze_opus_decoder.cpp"
        "base64_codec.cpp"
        "simple_ring_buffer.c"
        "audio_uplink.cpp"
        "audio_downlink.cpp"
        "opus_buffer.c"
    INCLUDE_DIRS "."
    REQUIRES
        espressif__esp_audio_codec
    PRIV_REQUIRES 
        esp_http_client 
        mbedtls 
        json 
        espressif__esp_websocket_client
)

