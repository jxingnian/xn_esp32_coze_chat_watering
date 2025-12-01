# iot_manager_mqtt 组件
#
# 封装 ESP-IDF `esp-mqtt` 客户端，并提供 Web 管理场景下的一整套 MQTT 管理能力：
#
# - 底层 MQTT 客户端封装（连接、重连、订阅、发布、消息回调）；
# - Web MQTT 管理器（`web_mqtt_manager`），统一管理与后台的交互；
# - 应用模块注册框架：按 Topic 前缀把消息分发给不同业务模块；
# - 内置设备注册模块和心跳模块，方便与 Web 后台联动。
#
# 通常配合仓库根目录下的 `xn_mqtt_server` PHP 网站一起使用。

