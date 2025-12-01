<?php
require_once __DIR__ . '/auth.php';
require_once __DIR__ . '/mqtt_config.php';
xn_require_login();

$db = xn_get_db();

$id = (int)($_GET['id'] ?? 0);

if ($id <= 0) {
    header('Location: index.php');
    exit;
}

if ($_SERVER['REQUEST_METHOD'] === 'POST') {
    $action = (string)($_POST['action'] ?? '');

    if ($action === 'delete') {
        $del = $db->prepare('DELETE FROM devices WHERE id = :id');
        $del->execute([':id' => $id]);
        header('Location: index.php');
        exit;
    }

    $mode = $action === 'start' ? 1 : 0;

    $upd = $db->prepare('UPDATE devices SET manage_mode = :m, updated_at = :u WHERE id = :id');
    $upd->execute([
        ':m'  => $mode,
        ':u'  => date('Y-m-d H:i:s'),
        ':id' => $id,
    ]);

    header('Location: device_manage.php?id=' . $id);
    exit;
}

$stmt = $db->prepare('SELECT * FROM devices WHERE id = :id');
$stmt->execute([':id' => $id]);
$device = $stmt->fetch();

if (!$device) {
    header('Location: index.php');
    exit;
}

// 解析在线状态
$now    = time();
$ts     = $device['last_seen_at'] ? strtotime($device['last_seen_at']) : 0;
$online = $ts && ($now - $ts <= XN_DEVICE_OFFLINE_SECONDS);

// 解析 meta_json 中的 WiFi 相关信息（由设备通过 MQTT 上报）
$wifiStatus = null;
$wifiSaved  = [];
if (!empty($device['meta_json'])) {
    $meta = json_decode($device['meta_json'], true);
    if (is_array($meta)) {
        if (isset($meta['wifi_status']) && is_array($meta['wifi_status'])) {
            $wifiStatus = $meta['wifi_status'];
        }
        if (isset($meta['wifi_saved']) && is_array($meta['wifi_saved'])) {
            // 结构形如 {"list":[{"ssid":"..."}, ...]}
            if (isset($meta['wifi_saved']['list']) && is_array($meta['wifi_saved']['list'])) {
                $wifiSaved = $meta['wifi_saved']['list'];
            } else {
                $wifiSaved = $meta['wifi_saved'];
            }
        }
    }
}

include __DIR__ . '/header.php';
?>
<h2>设备管理：<?php echo htmlspecialchars($device['device_id'], ENT_QUOTES, 'UTF-8'); ?></h2>
<p>
    在线状态：
    <?php if ($online): ?>
        <span class="status-dot status-online"></span>在线
    <?php else: ?>
        <span class="status-dot status-offline"></span>离线
    <?php endif; ?>
</p>
<p>最后在线时间：<?php echo $device['last_seen_at'] ? htmlspecialchars($device['last_seen_at'], ENT_QUOTES, 'UTF-8') : '-'; ?></p>
<p>当前管理模式：
    <?php if ((int)$device['manage_mode'] === 1): ?>
        <span class="badge badge-manage-on">管理中</span>
    <?php else: ?>
        <span class="badge badge-manage-off">空闲</span>
    <?php endif; ?>
</p>

<form method="post" style="margin-top: 12px;">
    <?php if ((int)$device['manage_mode'] === 1): ?>
        <button type="submit" name="action" value="stop" class="btn btn-danger">结束管理</button>
    <?php else: ?>
        <button type="submit" name="action" value="start" class="btn btn-primary">进入管理模式</button>
    <?php endif; ?>
    <button type="submit" name="action" value="delete" class="btn btn-danger" style="margin-left:8px;" onclick="return confirm('确定要删除这个设备吗？此操作不可恢复。');">删除设备</button>
    <a href="index.php" class="btn" style="margin-left:8px;">返回</a>
</form>

<p style="margin-top:16px; font-size:13px; color:#666;">
    当设备处于“管理中”状态时，可由 MQTT 规则或设备主动轮询后台接口，
    根据 manage_mode 字段决定是否进入“前后台通讯/暂停其他任务”模式。
</p>

<!-- WiFi 状态与管理区域 -->
<h3 style="margin-top:24px;">当前 WiFi 状态</h3>
<div style="margin-top:8px; font-size:14px;">
    <?php if ($wifiStatus): ?>
        <p>
            连接状态：
            <?php if (!empty($wifiStatus['connected'])): ?>
                <span class="status-dot status-online"></span>已连接
            <?php else: ?>
                <span class="status-dot status-offline"></span>未连接
            <?php endif; ?>
        </p>
        <p>当前 SSID：<?php echo htmlspecialchars($wifiStatus['ssid'] ?? '-', ENT_QUOTES, 'UTF-8'); ?></p>
        <p>当前 IP：<?php echo htmlspecialchars($wifiStatus['ip'] ?? '-', ENT_QUOTES, 'UTF-8'); ?></p>
        <p>RSSI：<?php echo isset($wifiStatus['rssi']) ? (int)$wifiStatus['rssi'] : 0; ?> dBm</p>
        <p>模式：<?php echo htmlspecialchars($wifiStatus['mode'] ?? '-', ENT_QUOTES, 'UTF-8'); ?></p>
    <?php else: ?>
        <p>暂未收到该设备上报的 WiFi 状态。</p>
    <?php endif; ?>
    <button id="btn-refresh-wifi" type="button" class="btn" style="margin-top:8px;">刷新 WiFi 状态</button>
</div>

<h3 style="margin-top:24px;">已保存 WiFi 列表</h3>
<div style="margin-top:8px;">
    <?php if (!empty($wifiSaved)): ?>
        <table class="table">
            <thead>
            <tr>
                <th>SSID</th>
                <th>操作</th>
            </tr>
            </thead>
            <tbody>
            <?php foreach ($wifiSaved as $w): ?>
                <?php $ssid = is_array($w) ? ($w['ssid'] ?? '') : (string)$w; ?>
                <?php if ($ssid === '') continue; ?>
                <tr>
                    <td><?php echo htmlspecialchars($ssid, ENT_QUOTES, 'UTF-8'); ?></td>
                    <td>
                        <button type="button" class="btn btn-primary btn-switch-wifi" data-ssid="<?php echo htmlspecialchars($ssid, ENT_QUOTES, 'UTF-8'); ?>">切换到此 WiFi</button>
                    </td>
                </tr>
            <?php endforeach; ?>
            </tbody>
        </table>
    <?php else: ?>
        <p style="font-size:13px; color:#666;">暂未收到该设备上报的已保存 WiFi 列表。</p>
    <?php endif; ?>
</div>

<!-- 通过 MQTT 下发新的 WiFi 配置 -->
<h3 style="margin-top:24px;">通过 MQTT 配置设备 WiFi</h3>
<form id="wifi-config-form" style="margin-top:12px; max-width:360px;">
    <div class="form-group">
        <label for="wifi-ssid">WiFi SSID：</label>
        <input type="text" id="wifi-ssid" name="ssid" class="input" required>
    </div>
    <div class="form-group" style="margin-top:8px;">
        <label for="wifi-password">WiFi 密码：</label>
        <input type="password" id="wifi-password" name="password" class="input">
    </div>
    <button type="submit" class="btn btn-primary" style="margin-top:12px;">发送 WiFi 配置</button>
</form>
<p style="margin-top:8px; font-size:13px; color:#666;">
    将通过 MQTT 向该设备下发 WiFi 配置，Topic 为
    <?php echo htmlspecialchars(XN_MQTT_BASE_TOPIC, ENT_QUOTES, 'UTF-8'); ?>/wifi/<?php echo htmlspecialchars($device['device_id'], ENT_QUOTES, 'UTF-8'); ?>/set。
    设备收到后会尝试连接对应 WiFi。
</p>

<script>
(function() {
    var form = document.getElementById('wifi-config-form');
    var deviceId = <?php echo json_encode($device['device_id']); ?>;
    var baseTopic = <?php echo json_encode(rtrim(XN_MQTT_BASE_TOPIC, '/')); ?>;

    // 下发新的 WiFi 配置
    if (form) {
        form.addEventListener('submit', function (e) {
            e.preventDefault();

            var ssidInput = document.getElementById('wifi-ssid');
            var pwdInput  = document.getElementById('wifi-password');

            var ssid = ssidInput.value.trim();
            var pwd  = pwdInput.value;

            if (!ssid) {
                alert('SSID 不能为空');
                return;
            }

            var payload = 'ssid=' + ssid + '\npassword=' + (pwd || '');
            var topic   = baseTopic + '/wifi/' + deviceId + '/set';

            fetch('api/mqtt_publish.php', {
                method: 'POST',
                headers: {
                    'Content-Type': 'application/json'
                },
                body: JSON.stringify({
                    topic: topic,
                    payload: payload,
                    retain: false
                })
            }).then(function (resp) {
                return resp.json();
            }).then(function (data) {
                if (data && data.status === 'ok') {
                    alert('WiFi 配置已发送，设备将尝试连接。');
                } else {
                    alert('发送失败：' + (data && data.message ? data.message : '未知错误'));
                }
            }).catch(function () {
                alert('网络错误，发送失败。');
            });
        });
    }

    // 切换到已保存 WiFi
    var btns = document.querySelectorAll('.btn-switch-wifi');
    btns.forEach(function (btn) {
        btn.addEventListener('click', function () {
            var ssid = this.getAttribute('data-ssid');
            if (!ssid) {
                return;
            }
            var payload = 'ssid=' + ssid;
            var topic   = baseTopic + '/wifi/' + deviceId + '/connect_saved';

            fetch('api/mqtt_publish.php', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ topic: topic, payload: payload, retain: false })
            }).then(function (resp) { return resp.json(); })
            .then(function (data) {
                if (data && data.status === 'ok') {
                    alert('切换 WiFi 指令已发送，稍后请刷新状态。');
                } else {
                    alert('发送失败：' + (data && data.message ? data.message : '未知错误'));
                }
            }).catch(function () {
                alert('网络错误，发送失败。');
            });
        });
    });

    // 刷新 WiFi 状态与列表：发送 get_status / get_saved 指令
    var btnRefresh = document.getElementById('btn-refresh-wifi');
    if (btnRefresh) {
        btnRefresh.addEventListener('click', function () {
            var topicStatus = baseTopic + '/wifi/' + deviceId + '/get_status';
            var topicSaved  = baseTopic + '/wifi/' + deviceId + '/get_saved';

            fetch('api/mqtt_publish.php', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ topic: topicStatus, payload: '', retain: false })
            }).catch(function () {});

            fetch('api/mqtt_publish.php', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ topic: topicSaved, payload: '', retain: false })
            }).catch(function () {});

            alert('已请求设备上报 WiFi 状态和已保存列表，请稍候刷新页面查看最新信息。');
        });
    }
})();
</script>

<?php include __DIR__ . '/footer.php'; ?>
