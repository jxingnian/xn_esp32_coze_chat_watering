<?php
// 设备管理状态查询接口
// 可由设备或 MQTT 规则定期查询，判断设备是否处于“管理模式”

require_once __DIR__ . '/../config.php';
require_once __DIR__ . '/../db.php';

header('Content-Type: application/json; charset=utf-8');

if (XN_INGEST_SHARED_SECRET !== '') {
    $token = (string)($_GET['token'] ?? '');
    if ($token !== XN_INGEST_SHARED_SECRET) {
        http_response_code(403);
        echo json_encode(['status' => 'error', 'message' => 'forbidden']);
        exit;
    }
}

$raw  = file_get_contents('php://input');
$data = json_decode($raw, true);

$clientId = '';
if (is_array($data) && isset($data['client_id'])) {
    $clientId = (string)$data['client_id'];
}

if ($clientId === '') {
    $clientId = (string)($_GET['client_id'] ?? $_POST['client_id'] ?? '');
}

if ($clientId === '') {
    http_response_code(400);
    echo json_encode(['status' => 'error', 'message' => 'missing client_id']);
    exit;
}

$db   = xn_get_db();
$stmt = $db->prepare('SELECT device_id, manage_mode FROM devices WHERE device_id = :id');
$stmt->execute([':id' => $clientId]);
$row = $stmt->fetch();

if (!$row) {
    echo json_encode([
        'status'      => 'ok',
        'exists'      => false,
        'manage_mode' => false,
    ]);
    exit;
}

echo json_encode([
    'status'      => 'ok',
    'exists'      => true,
    'device_id'   => $row['device_id'],
    'manage_mode' => ((int)$row['manage_mode'] === 1),
]);
