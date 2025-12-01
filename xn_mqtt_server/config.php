<?php
/*
 * @Author: 星年 && jixingnian@gmail.com
 * @Date: 2025-11-24 14:07:05
 * @LastEditors: xingnian jixingnian@gmail.com
 * @LastEditTime: 2025-11-24 20:38:08
 * @FilePath: \xn_esp32_web_mqtt_manager\xn_mqtt_server\config.php
 * @Description: 基础配置
 * 
 * Copyright (c) 2025 by ${git_name_email}, All Rights Reserved. 
 */

// MySQL 数据库连接配置（请按宝塔中创建的数据库信息修改）
define('XN_DB_HOST', '127.0.0.1');      // 数据库主机
define('XN_DB_PORT', 3306);             // 端口
define('XN_DB_NAME', 'xn_mqtt');   // 数据库名
define('XN_DB_USER', 'xn_mqtt');   // 用户名
define('XN_DB_PASS', 'xn_mqtt_pass');   // 密码
define('XN_DB_CHARSET', 'utf8mb4');     // 字符集

// 管理后台 Session 名称
define('XN_SESSION_NAME', 'xn_mqtt_admin');

// 默认管理员账号（首次运行会自动创建，登录后请尽快修改密码）
define('XN_DEFAULT_ADMIN_USER', 'admin');
define('XN_DEFAULT_ADMIN_PASS', 'admin123');

// HTTP API 共享密钥（供 MQTT 规则引擎调用），为空字符串表示不校验
// 强烈建议上线前改成复杂随机字符串，并在规则中以 ?token=XXX 方式传入
define('XN_INGEST_SHARED_SECRET', 'Li2k0e3mVRW4akNjvmwK');

// 多久未收到心跳视为离线（秒）
define('XN_DEVICE_OFFLINE_SECONDS', 90);
