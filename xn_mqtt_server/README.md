# xn_mqtt_server

MQTT 设备管理后台（PHP + MySQL），用于配合 ESP32 端的 MQTT 管理组件和 MQTT 服务器规则引擎，实现：

- 设备在线状态统计与展示
- 设备列表、搜索与排序
- 手动切换“设备管理模式”（方便前后台联调时暂停其他任务）
- 接收来自 MQTT 规则转发的心跳 / 业务消息，更新设备在线信息
- 管理员登录、退出与修改密码

将本目录整体打包丢到宝塔的 PHP 站点根目录，再配置好 MySQL 与 MQTT 规则后即可使用。

---

## 1. 目录结构

```text
xn_mqtt_server/
├─ config.php           # 全局配置（MySQL、Session、共享密钥等）
├─ db.php               # PDO 封装与数据表初始化
├─ auth.php             # 登录鉴权相关函数
├─ header.php           # 公共页头、基础样式
├─ footer.php           # 公共页脚
├─ login.php            # 登录页
├─ logout.php           # 退出登录
├─ change_password.php  # 修改管理员密码
├─ index.php            # 后台首页（设备统计 + 列表）
├─ device_manage.php    # 单设备管理页面（切换管理模式）
└─ api/
   ├─ mqtt_ingest.php        # MQTT 规则 HTTP 转发入口，更新在线状态
   └─ device_manage_status.php # 设备管理状态查询接口
```

> 说明：之前的 SQLite 版本已经替换为 MySQL，不再使用 `data/app.sqlite` 文件。

---

## 2. MySQL 配置

编辑 `config.php`，按你在宝塔中创建的数据库信息修改：

```php
// MySQL 数据库连接配置
define('XN_DB_HOST', '127.0.0.1');  // 数据库主机
define('XN_DB_PORT', 3306);         // 端口
define('XN_DB_NAME', 'xn_mqtt');    // 数据库名
define('XN_DB_USER', 'xn_mqtt');    // 用户名
define('XN_DB_PASS', 'xn_mqtt_pass'); // 密码
define('XN_DB_CHARSET', 'utf8mb4'); // 字符集
```

首次通过浏览器访问后台时，代码会自动：

- 使用上述配置连接 MySQL；
- 若不存在则创建 `users` 和 `devices` 两张表；
- 若 `users` 表为空，则创建一个默认管理员账户：
  - 用户名：`XN_DEFAULT_ADMIN_USER`（默认 `admin`）
  - 密码：`XN_DEFAULT_ADMIN_PASS`（默认 `admin123`）

> 建议首次登录后立即到“修改密码”页面更改默认密码。

---

## 3. 管理后台功能说明

### 3.1 登录 / 退出 / 修改密码

- `login.php`：管理员登录界面；
- 登录成功后可访问：
  - `index.php`：首页 / 设备管理；
  - `change_password.php`：修改当前管理员密码；
  - `logout.php`：退出登录。

### 3.2 设备统计与列表（index.php）

首页显示：

- 设备总数；
- 在线设备数量（根据 `last_seen_at` 是否在 `XN_DEVICE_OFFLINE_SECONDS` 内判断）；
- 设备列表：
  - 支持按设备 ID / 名称搜索；
  - 支持按最后在线时间 / 设备 ID / 创建时间排序（升序/降序）；
  - 显示在线状态、最后在线时间、当前管理模式（空闲/管理中）；
  - 提供“设备管理”按钮跳转到单设备管理页。

### 3.3 设备管理模式（device_manage.php）

单设备管理页可以：

- 查看该设备在线状态、最后在线时间；
- 启动或结束“管理模式”（数据库的 `manage_mode` 字段）：
  - 进入管理模式：`manage_mode` 置为 1；
  - 结束管理模式：`manage_mode` 置为 0；

这个标志可以结合前端设备或 MQTT 规则使用，例如：

- 设备定期请求 `api/device_manage_status.php`，根据返回的 `manage_mode` 决定是否：
  - 切换到“前后台通讯模式”、暂停其他业务任务；
  - 或恢复正常运行。

---

## 4. MQTT 规则转发与 API

### 4.1 共享密钥

为了避免外部恶意访问，可在 `config.php` 中设置一个共享密钥：

```php
define('XN_INGEST_SHARED_SECRET', 'your-strong-secret');
```

然后 MQTT 规则在访问 HTTP 接口时，带上：`?token=your-strong-secret`。

### 4.2 mqtt_ingest.php

`api/mqtt_ingest.php` 用于接收 MQTT 服务器转发的消息，主要功能：

- 按 `client_id` 创建或更新设备记录；
- 更新 `last_seen_at`、`last_ip`、`updated_at` 字段，用于在线统计。

支持两种常见参数格式：

- JSON POST：
  ```json
  {
    "client_id": "dev-001",
    "topic": "xn/esp/hb",
    "payload": "..."   // 可以是字符串或对象
  }
  ```
- 表单 POST：
  ```
  client_id=dev-001&topic=xn/esp/hb&payload=...
  ```

**在 MQTT 服务器中配置规则的思路：**

- 条件：匹配设备上行 Topic（例如 `xn/esp/hb`、`xn/esp/reg/query`，或统一使用 `xn/esp/#`）；
- 动作：HTTP POST 到：
  - `http://你的域名/api/mqtt_ingest.php?token=你的共享密钥`
  - 使用 HTTP 动作的默认 JSON 请求体（无需自定义 Body 模板），其中会包含规则 SQL 导出的 `client_id` / `topic` / `payload` 字段。

只要设备通过 MQTT 按约定 Topic 上行 + 规则转发到本接口，后台就会自动维护设备列表和在线状态。

### 4.4 网站作为 MQTT 客户端（发送指令）

网站本身也可以作为一个 MQTT 客户端连接 EMQX，用于向设备发送指令：

- 配置文件：`mqtt_config.php`

  ```php
  define('XN_MQTT_HOST', '127.0.0.1');   // EMQX 地址
  define('XN_MQTT_PORT', 1883);          // 端口
  define('XN_MQTT_CLIENT_ID', 'xn_mqtt_server');
  define('XN_MQTT_USERNAME', 'xn_mqtt');
  define('XN_MQTT_PASSWORD', 'xn_mqtt_pass');
  define('XN_MQTT_KEEPALIVE', 60);
  define('XN_MQTT_BASE_TOPIC', 'xn/web');
  ```

- MQTT 客户端类：`lib/MqttClient.php`（纯 PHP 实现的简单 MQTT 3.1.1 客户端）。

- 发送指令接口：`api/mqtt_publish.php`

  仅允许已登录的后台管理员调用，可通过 AJAX/POST 调用：

  ```http
  POST /api/mqtt_publish.php
  Content-Type: application/json

  {
    "topic": "xn/web/dev-001/cmd",
    "payload": "{\"action\":\"reboot\"}",
    "retain": false
  }
  ```

  服务端会使用 `mqtt_config.php` 中的配置连接 EMQX，并向指定 Topic 发布消息。

> 注意：订阅场景建议仍由 EMQX 通过 HTTP 规则转发到网站；网站内置的 MQTT 客户端主要用于向设备主动发送指令。

#### 4.5 EMQX 规则配置示例

以 EMQX Dashboard 为例（其他 MQTT 服务器可类比）：

1. 在 **规则** 中新建规则，SQL 示例：

   ```sql
   SELECT
     clientid AS client_id,
     topic,
     payload
   FROM
     "xn/esp/#"
   ```

   - `"xn/esp/#"` 用于匹配设备上行 Topic（例如 `xn/esp/hb`、`xn/esp/reg/query`）；
   - 也可以只统计心跳 Topic，如 `"xn/esp/hb"`。

2. 为规则添加 **动作**：HTTP 请求（发送数据到 Web 服务器）。

   - 方法：`POST`
   - URL：`http://你的域名/api/mqtt_ingest.php?token=你的共享密钥`
   - Body：保持 EMQX 默认设置（同样为 `application/json`），**不需要额外配置模板**。

   EMQX 会自动将规则 SQL 的输出序列化为 JSON，例如：

   ```json
   {
     "client_id": "ESP32_XXXXXX",
     "topic": "xn/esp/hb",
     "payload": "..."
   }
   ```

   这样，EMQX 收到匹配 Topic 的消息后，就会把 `client_id`、`topic`、`payload` 通过 HTTP 转发到网站，由网站更新设备在线状态。

### 4.3 device_manage_status.php

`api/device_manage_status.php` 用于让设备或规则查询当前设备的“管理模式”状态：

- 入参：`client_id`（可通过 JSON / GET / POST 传递）；
- 返回示例：

```json
{
  "status": "ok",
  "exists": true,
  "device_id": "dev-001",
  "manage_mode": true
}
```

设备侧可以周期性调用该接口：

- `manage_mode = true`：进入“后台管理模式”，暂停普通业务，专注与后台交互；
- `manage_mode = false`：恢复正常工作模式。

---

## 5. 在宝塔上的部署步骤（示例）

1. **创建 PHP 站点**：
   - 在宝塔中新建网站，选择 PHP 版本（建议 7.4 或 8.x，开启 `pdo_mysql` 扩展）。

2. **创建 MySQL 数据库**：
   - 新建数据库，例如：
     - 数据库名：`xn_mqtt`
     - 用户名：`xn_mqtt`
     - 密码：`xn_mqtt_pass`
   - 把这些值同步填入 `config.php` 中对应常量。

3. **上传代码**：
   - 将整个 `xn_mqtt_server` 目录压缩为 zip；
   - 上传到网站根目录并解压；
   - 确认文件权限正常（一般使用宝塔默认即可）。

4. **首次访问**：
   - 浏览器访问 `http://你的域名/login.php`；
   - 使用默认管理员登录：`admin` / `admin123`；
   - 登录后立即进入“修改密码”页面更改密码。

5. **配置 MQTT 规则**：
   - 在 MQTT 服务器上配置规则，将心跳等消息转发到：
     - `http://你的域名/api/mqtt_ingest.php?token=你的共享密钥`
   - 设备若需要感知管理模式，可另外调用：
     - `http://你的域名/api/device_manage_status.php?token=你的共享密钥`。

完成以上步骤后，配合 ESP32 端的 MQTT 管理组件和心跳/注册协议，即可在浏览器中查看设备在线情况并进入“管理模式”进行前后台联调。
