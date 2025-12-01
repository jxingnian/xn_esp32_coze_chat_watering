<!--
 * @Author: 星年 && jixingnian@gmail.com
 * @Date: 2025-12-01 13:45:08
 * @LastEditors: xingnian jixingnian@gmail.com
 * @LastEditTime: 2025-12-01 15:21:35
 * @FilePath: \xn_esp32_coze_chat_watering\README.md
 * @Description: 
 * 
 * Copyright (c) 2025 by ${git_name_email}, All Rights Reserved. 
-->

# xn_esp32_coze_chat_watering 项目说明

## 1. 项目简介

本项目是一个基于 **ESP32 + MQTT + PHP 后台** 的物联网示例工程，包含：

- **ESP32 固件**：
  - 网页 WiFi 配网组件（SoftAP + Web 页面）；
  - 通过 MQTT 与后端交互的通用管理框架；
  - 自动浇花应用：
    - 网页点击开/关浇花；
    - 每天固定时间自动浇水若干秒（定时计划）。
- **PHP 后台（xn_mqtt_server）**：
  - 设备注册与管理后台；
  - 集成内置 MQTT 客户端，提供简单 API；
  - 通过 MQTT 规则引擎接收设备上报的状态（WiFi / 浇花 / 定时计划）。

整体架构：

```text
ESP32 <——MQTT——> EMQX 等 Broker <——HTTP + MQTT——> xn_mqtt_server(PHP 后台) <——Web 浏览器
```

你可以在后台页面中查看设备在线状态、WiFi 状态、浇花状态，并下发浇花开关和每天定时浇水计划。

---

## 2. 目录结构概览

只列出与本项目相关的主要目录：

- **main/**
  - `main.c`：ESP32 应用入口，初始化 WiFi 管理、Web MQTT 管理器和各类 MQTT App。
  - `mqtt_app/`
    - `wifi_config_app.c`：通过 MQTT 从后台接收 WiFi 配置信息、上报 WiFi 状态。
    - `watering_app.c`：通过 MQTT 控制浇花 GPIO，并实现“每天定时浇水”计划。

- **components/**
  - `xn_web_wifi_manger/`：网页 WiFi 配网组件（SoftAP + Web + HTTP API）。
  - `xn_iot_manager_mqtt/`：Web MQTT 管理器，负责统一处理 MQTT 连接、分发消息给各 App。
  - 其他如音频、Coze Chat、lottie 等组件可按需要启用/注释。

- **xn_mqtt_server/**（PHP 后台）
  - `config.php`：数据库、后台登录和 MQTT 规则口令等基础配置。
  - `mqtt_config.php`：MQTT 连接参数与 Topic 前缀配置。
  - `header.php` / `footer.php`：后台通用头尾模板。
  - `index.php`：设备列表页面。
  - `device_manage.php`：单设备管理页（WiFi 状态、浇花状态、定时计划配置等）。
  - `api/`
    - `mqtt_ingest.php`：MQTT 规则引擎 HTTP 转发入口，处理设备上报消息。
    - `mqtt_publish.php`：后台向设备下发 MQTT 消息的简单 API。
    - `device_manage_status.php` 等：辅助接口。

---

## 3. 快速开始

### 3.1 准备环境

- 硬件：ESP32 开发板（如 ESP32-S3）。
- 软件：
  - ESP-IDF 环境（用于编译烧录固件）；
  - Web 服务器（如宝塔 + PHP + MySQL）；
  - MQTT Broker（如 EMQX）。

### 3.2 配置 PHP 后台

1. **数据库配置**：
   - 编辑 `xn_mqtt_server/config.php`，根据你在宝塔中创建的数据库信息修改：
     - `XN_DB_HOST` / `XN_DB_PORT` / `XN_DB_NAME` / `XN_DB_USER` / `XN_DB_PASS`；
   - 配置 `XN_DEFAULT_ADMIN_USER` / `XN_DEFAULT_ADMIN_PASS` 作为初始后台账号。

2. **MQTT 配置**：
   - 编辑 `xn_mqtt_server/mqtt_config.php`，设置：
     - `XN_MQTT_HOST` / `XN_MQTT_PORT` / `XN_MQTT_USERNAME` / `XN_MQTT_PASSWORD` 等；
     - `XN_MQTT_BASE_TOPIC`（默认例如 `xn/web`）；
     - `XN_MQTT_UPLINK_BASE_TOPIC`（默认例如 `xn/esp`）。

3. **部署代码**：
   - 将 `xn_mqtt_server` 部署到你的 PHP 站点根目录或子目录；
   - 确保 `config.php`、`mqtt_config.php` 可被正确加载。

4. **初始化数据库**：
   - 按 `xn_mqtt_server` 目录下的 SQL 说明（如有提供）或后台自动建表逻辑准备好数据表。

5. **MQTT 规则引擎**：
   - 在 EMQX 中配置规则，将设备上行 Topic（如 `xn/esp/#`）转发到：
     - `POST http(s)://<你的域名或IP>/xn_mqtt_server/api/mqtt_ingest.php?token=...`
   - `token` 应与 `config.php` 中的 `XN_INGEST_SHARED_SECRET` 保持一致。

### 3.3 编译和烧录 ESP32

在工程根目录执行（根据你的芯片型号调整 target）：

```bash
idf.py set-target esp32s3   # 或 esp32 等
idf.py build
idf.py flash
idf.py monitor
```

烧录成功后，串口会打印日志，WiFi 管理和 MQTT 连接过程可在 log 中看到。

---

## 4. ESP32 端主要逻辑

### 4.1 WiFi 管理与网页配网

- 通过 `wifi_manage_init()` 初始化 WiFi 管理模块：
  - 支持 STA + AP 模式；
  - 提供 SoftAP + Web 页面用于扫描 WiFi 和输入密码；
  - 支持在 NVS 中保存多组 AP，并自动重连。

### 4.2 Web MQTT 管理器

- `web_mqtt_manager` 统一管理 MQTT 连接，负责：
  - 连接配置在 `main.c` 中设置（Broker URI、base_topic 等）；
  - 调用 `web_mqtt_manager_register_app("wifi", ...)` / `web_mqtt_manager_register_app("watering", ...)` 注册子应用；
  - 收到消息后根据 `base_topic/<app>/<device_id>/<cmd>` 分发给对应 App。

### 4.3 WiFi 配置 MQTT App（wifi_config_app）

- 主要 Topic：
  - 下行：
    - `xn/web/wifi/<device_id>/set`：下发新的 WiFi SSID/密码（payload 为 `key=value` 多行文本）；
    - `xn/web/wifi/<device_id>/get_status`：请求当前 WiFi 状态；
    - `xn/web/wifi/<device_id>/get_saved`：请求已保存 WiFi 列表；
    - `xn/web/wifi/<device_id>/connect_saved`：请求切换到某个已保存的 SSID。
  - 上行：
    - `xn/esp/wifi/<device_id>/status`：JSON 格式的 WiFi 状态；
    - `xn/esp/wifi/<device_id>/saved`：JSON 格式的已保存 WiFi 列表。

后台通过 `mqtt_ingest.php` 将这些状态写入 `devices.meta_json`，供管理页面展示。

### 4.4 自动浇花 MQTT App（watering_app）

#### 手动浇花控制

- 下行（Web → 设备）：
  - `xn/web/watering/<device_id>/set`：payload 为 `"on"` / `"off"`，控制浇花电机 GPIO 开关；
  - `xn/web/watering/<device_id>/get_status`：请求当前浇花状态。

- 上行（设备 → Web）：
  - `xn/esp/watering/<device_id>/status`：
    - payload JSON：`{"on": true}` 或 `{"on": false}`。

#### 每天固定时间定时浇水

定时计划由设备本地执行，通过 FreeRTOS 任务结合 `time()` 计算每天指定时间点。

- 下行配置计划：
  - Topic：`xn/web/watering/<device_id>/set_plan`
  - payload 为多行文本：

    ```text
    enabled=1
    hour=7
    minute=30
    duration_s=10
    ```

    字段说明：

    - `enabled`：`1` 启用计划，`0` 停用；
    - `hour`：0~23；
    - `minute`：0~59；
    - `duration_s`：每次浇水时长（秒），1~600。

- 下行读取计划：
  - Topic：`xn/web/watering/<device_id>/get_plan`，payload 为空。

- 上行上报计划：
  - Topic：`xn/esp/watering/<device_id>/plan`
  - payload JSON 示例：

    ```json
    {"enabled": true, "hour": 7, "minute": 30, "duration_s": 10}
    ```

后台会把 `watering_status` 和 `watering_plan` 保存在 `devices.meta_json` 中，供前端展示。

---

## 5. 后台 Web 管理界面

访问 `xn_mqtt_server` 部署地址，登录后台后可以看到：

- **设备列表（index.php）**：
  - 显示设备 ID、在线状态、最后心跳时间等；
  - 进入单设备管理页。

- **单设备管理页（device_manage.php）**：
  - 在线/离线状态、管理模式；
  - 当前 WiFi 状态、已保存 WiFi 列表以及通过 MQTT 配网；
  - 自动浇花：
    - 当前浇花状态（开/关）；
    - 按钮：开启浇花 / 关闭浇花 / 刷新状态；
  - 定时浇水计划：
    - 是否启用；
    - 每天几点几分浇水、持续多少秒；
    - “保存定时计划” / “刷新计划” 按钮，实际通过 MQTT 的 `set_plan` / `get_plan` 与设备交互。

---

## 6. 注意事项

- **时间同步**：
  - 每天固定时间的定时浇水依赖 ESP32 的本地时间，需要通过 SNTP 或其他方式同步时间；
  - 若时间不正确，浇水时间也会偏移。

- **安全性**：
  - `XN_INGEST_SHARED_SECRET` 建议在线上环境使用随机复杂字符串，并在 MQTT 规则中以 `?token=` 形式传入；
  - 数据库账号、密码请勿直接暴露在公共仓库。

- **硬件安全**：
  - 浇花电机 GPIO 仅输出控制信号，实际驱动水泵需使用 MOSFET / 继电器等驱动电路；
  - 注意防水、防反接、电源能力等硬件设计问题。

---

## 7. 后续扩展方向

- 支持一天多次浇水计划（早晚浇水等）；
- 接入土壤湿度传感器，根据湿度自动判断是否需要浇水；
- 在后台增加统计图表（例如最近一周浇水记录）；
- 接入更多基于 MQTT 的应用模块（如灯光控制、环境监测等）。

