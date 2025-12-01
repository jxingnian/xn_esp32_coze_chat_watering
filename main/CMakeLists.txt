idf_component_register(SRCS "main.c" 
                            "coze_chat_app/coze_chat_app.c"
                            "audio_app/audio_config_app.c"
                            "lottie_app/lottie_app.c"
                            "mqtt_app/wifi_config_app.c"
                            "mqtt_app/watering_app.c"
                       PRIV_REQUIRES 
                            xn_web_wifi_manger 
                            xn_coze_chat 
                            esp_timer
                            xn_audio_manager
                            xn_lottie_manager
                            xn_iot_manager_mqtt
                       INCLUDE_DIRS "." 
                            "coze_chat_app"
                            "audio_app"
                            "lottie_app")
