<?php
/*
 * @Author: 星年 && jixingnian@gmail.com
 * @Date: 2025-11-24 14:56:51
 * @LastEditors: xingnian jixingnian@gmail.com
 * @LastEditTime: 2025-11-24 15:01:19
 * @FilePath: \xn_web_mqtt_manager\xn_mqtt_server\mqtt_config.php
 * @Description: 
 * 
 * Copyright (c) 2025 by ${git_name_email}, All Rights Reserved. 
 */
// MQTT 服务器（EMQX）连接配置

// EMQX 地址和端口
define('XN_MQTT_HOST', '127.0.0.1');     // EMQX 主机地址（可用内网地址或域名）
define('XN_MQTT_PORT', 1883);            // MQTT 端口（默认 1883）

// 网站后台作为一个 MQTT 客户端连接 EMQX 时使用的 client_id
// 建议在同一集群中保持唯一（如包含服务器名后缀）
define('XN_MQTT_CLIENT_ID', 'xn_mqtt_server');

// MQTT 用户名 / 密码
// 建议与设备端组件的默认值保持一致，或者统一约定好：
//   WEB_MQTT_DEFAULT_USERNAME / WEB_MQTT_DEFAULT_PASSWORD
// 然后在 EMQX 中创建对应账号。
define('XN_MQTT_USERNAME', 'xn_mqtt_server');
define('XN_MQTT_PASSWORD', 'xn_mqtt_server_pass');

// 保活时间（秒）
define('XN_MQTT_KEEPALIVE', 60);

// 可选：网站常用的 Topic 前缀（应与设备端 base_topic 对应）
define('XN_MQTT_BASE_TOPIC', 'xn/web');
define('XN_MQTT_UPLINK_BASE_TOPIC', 'xn/esp');
