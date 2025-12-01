idf_component_register(
    SRCS
        "src/web_mqtt_manager.c"
        "src/mqtt_module.c"
        "src/mqtt_reg_module.c"
        "src/mqtt_heartbeat_module.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        mqtt
)

