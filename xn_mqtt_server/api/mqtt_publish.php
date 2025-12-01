<?php
// 网站后台通过 MQTT 向设备发送指令的简单接口
// 需要管理员登录后调用（例如从后台页面通过 AJAX 调用本接口）。

require_once __DIR__ . '/../auth.php';
require_once __DIR__ . '/../mqtt_config.php';
require_once __DIR__ . '/../lib/MqttClient.php';

header('Content-Type: application/json; charset=utf-8');

// 要求后台登录
xn_require_login();

$raw = file_get_contents('php://input');
$data = json_decode($raw, true);

if (!is_array($data)) {
    $data = $_POST;
}

$topic   = isset($data['topic']) ? (string)$data['topic'] : '';
$payload = isset($data['payload']) ? (string)$data['payload'] : '';
$retain  = isset($data['retain']) ? (bool)$data['retain'] : false;

if ($topic === '') {
    http_response_code(400);
    echo json_encode(['status' => 'error', 'message' => 'missing topic']);
    exit;
}

try {
    $client = new XnMqttClient(
        XN_MQTT_HOST,
        XN_MQTT_PORT,
        XN_MQTT_CLIENT_ID,
        XN_MQTT_USERNAME,
        XN_MQTT_PASSWORD,
        XN_MQTT_KEEPALIVE
    );

    $client->publish($topic, $payload, $retain);

    echo json_encode(['status' => 'ok']);
} catch (Throwable $e) {
    http_response_code(500);
    echo json_encode([
        'status'  => 'error',
        'message' => $e->getMessage(),
    ]);
}
