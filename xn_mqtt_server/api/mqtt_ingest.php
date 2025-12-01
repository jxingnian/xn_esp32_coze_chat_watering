<?php
// MQTT 规则引擎 HTTP 转发入口
// 建议配置为：当收到心跳或业务消息时，POST JSON 到本接口

require_once __DIR__ . '/../config.php';
require_once __DIR__ . '/../db.php';
require_once __DIR__ . '/../mqtt_config.php';
require_once __DIR__ . '/../lib/MqttClient.php';

header('Content-Type: application/json; charset=utf-8');

// 简单令牌校验
if (XN_INGEST_SHARED_SECRET !== '') {
    $token = (string)($_GET['token'] ?? '');
    if ($token !== XN_INGEST_SHARED_SECRET) {
        http_response_code(403);
        echo json_encode(['status' => 'error', 'message' => 'forbidden']);
        exit;
    }
}

$raw  = file_get_contents('php://input');
$logLine = sprintf(
    "[%s] ip=%s raw=%s\n",
    date('Y-m-d H:i:s'),
    $_SERVER['REMOTE_ADDR'] ?? '-',
    $raw
);
@file_put_contents(__DIR__ . '/../mqtt_ingest.log', $logLine, FILE_APPEND);
$data = json_decode($raw, true);

$clientId = '';
$topic    = '';
$payload  = '';

if (is_array($data) && isset($data['client_id'])) {
    $clientId = (string)$data['client_id'];
    $topic    = isset($data['topic']) ? (string)$data['topic'] : '';
    if (array_key_exists('payload', $data)) {
        $payload = is_string($data['payload']) ? $data['payload'] : json_encode($data['payload']);
    }
}

if ($clientId === '') {
    $clientId = isset($_POST['client_id']) ? (string)$_POST['client_id'] : '';
    if (isset($_POST['topic'])) {
        $topic = (string)$_POST['topic'];
    }
    if (isset($_POST['payload'])) {
        $payload = (string)$_POST['payload'];
    }
}

if ($clientId === '') {
    http_response_code(400);
    echo json_encode(['status' => 'error', 'message' => 'missing client_id']);
    exit;
}

$db     = xn_get_db();
$device = xn_upsert_device($db, $clientId);

$now = date('Y-m-d H:i:s');
$ip  = $_SERVER['REMOTE_ADDR'] ?? null;

// 简单更新在线信息
 $upd = $db->prepare('UPDATE devices SET last_seen_at = :ls, last_ip = :ip, updated_at = :u WHERE id = :id');
 $upd->execute([
    ':ls' => $now,
    ':ip' => $ip,
    ':u'  => $now,
    ':id' => $device['id'],
]);

$insMsg = $db->prepare('INSERT INTO mqtt_messages (client_id, topic, payload, created_at)
                         VALUES (:c, :t, :p, :ts)');
$insMsg->execute([
    ':c'  => $clientId,
    ':t'  => $topic,
    ':p'  => $payload,
    ':ts' => $now,
]);

// 处理设备注册查询：当 Topic 为上行前缀 + "/reg/query" 时，标记已注册并回复一条 MQTT 消息
if ($topic !== '') {
    $regPrefix = XN_MQTT_UPLINK_BASE_TOPIC . '/reg/query';
    if (strpos($topic, $regPrefix) === 0) {
        // 在 meta_json 中记录注册标志
        $meta = [];
        if (!empty($device['meta_json'])) {
            $decoded = json_decode($device['meta_json'], true);
            if (is_array($decoded)) {
                $meta = $decoded;
            }
        }
        $meta['registered']     = true;
        $meta['registered_at']  = $now;

        $metaJson = json_encode($meta, JSON_UNESCAPED_UNICODE);

        $updMeta = $db->prepare('UPDATE devices SET meta_json = :meta, updated_at = :u WHERE id = :id');
        $updMeta->execute([
            ':meta' => $metaJson,
            ':u'    => $now,
            ':id'   => $device['id'],
        ]);

        // 通过网站内置 MQTT 客户端给设备回复一条注册成功消息
        try {
            $mqtt = new XnMqttClient(
                XN_MQTT_HOST,
                XN_MQTT_PORT,
                XN_MQTT_CLIENT_ID,
                XN_MQTT_USERNAME,
                XN_MQTT_PASSWORD,
                XN_MQTT_KEEPALIVE
            );

            $replyTopic = rtrim(XN_MQTT_BASE_TOPIC, '/') . '/reg/' . $clientId . '/resp';
            $replyBody  = json_encode([
                'status'     => 'ok',
                'device_id'  => $clientId,
                'registered' => true,
            ], JSON_UNESCAPED_UNICODE);

            $mqtt->publish($replyTopic, $replyBody, false);
        } catch (Throwable $e) {
            // 注册回复失败不会影响 HTTP 返回，必要时可在这里加日志
        }
    }
}

// 处理 WiFi 状态上报与已保存列表上报：
//  - xn/esp/wifi/<device_id>/status 上报当前 WiFi 连接状态（JSON）
//  - xn/esp/wifi/<device_id>/saved  上报已保存 WiFi 列表（JSON）
if ($topic !== '' && $payload !== '') {
    $wifiStatusPrefix = XN_MQTT_UPLINK_BASE_TOPIC . '/wifi/' . $clientId . '/status';
    $wifiSavedPrefix  = XN_MQTT_UPLINK_BASE_TOPIC . '/wifi/' . $clientId . '/saved';

    $needUpdateMeta = false;
    $meta           = [];

    if (!empty($device['meta_json'])) {
        $decoded = json_decode($device['meta_json'], true);
        if (is_array($decoded)) {
            $meta = $decoded;
        }
    }

    if (strpos($topic, $wifiStatusPrefix) === 0) {
        $status = json_decode($payload, true);
        if (is_array($status)) {
            $meta['wifi_status'] = $status;
            $needUpdateMeta      = true;
        }
    } elseif (strpos($topic, $wifiSavedPrefix) === 0) {
        $saved = json_decode($payload, true);
        if (is_array($saved)) {
            $meta['wifi_saved'] = $saved;
            $needUpdateMeta     = true;
        }
    }

    if ($needUpdateMeta) {
        $metaJson = json_encode($meta, JSON_UNESCAPED_UNICODE);
        $updMeta2 = $db->prepare('UPDATE devices SET meta_json = :meta, updated_at = :u WHERE id = :id');
        $updMeta2->execute([
            ':meta' => $metaJson,
            ':u'    => $now,
            ':id'   => $device['id'],
        ]);
    }
}

// 如需根据 topic / payload 做更进一步的业务（如注册、配置），
// 可在此处解析 $topic / $payload 并更新 meta_json 等字段。

echo json_encode(['status' => 'ok']);
