<?php
require_once __DIR__ . '/auth.php';
xn_require_login();

$db = xn_get_db();

$lastMsg = null;
try {
    $stmtLast = $db->query('SELECT client_id, topic, payload, created_at FROM mqtt_messages ORDER BY id DESC LIMIT 1');
    $lastMsg  = $stmtLast->fetch();
} catch (Throwable $e) {
    $lastMsg = null;
}

$q     = trim((string)($_GET['q'] ?? ''));
$sort  = (string)($_GET['sort'] ?? 'last_seen');
$order = (string)($_GET['order'] ?? 'desc');

$sortMap = [
    'device_id' => 'device_id',
    'created'   => 'created_at',
    'last_seen' => 'last_seen_at',
];
$sortCol = $sortMap[$sort] ?? 'last_seen_at';
$order   = strtolower($order) === 'asc' ? 'ASC' : 'DESC';

$sql    = 'SELECT * FROM devices WHERE 1';
$params = [];

if ($q !== '') {
    $sql           .= ' AND (device_id LIKE :q OR name LIKE :q)';
    $params[':q'] = '%' . $q . '%';
}

$sql .= " ORDER BY $sortCol $order";

$stmt    = $db->prepare($sql);
$stmt->execute($params);
$devices = $stmt->fetchAll();

$total  = count($devices);
$online = 0;
$now    = time();

foreach ($devices as &$d) {
    $ts = $d['last_seen_at'] ? strtotime($d['last_seen_at']) : 0;
    $d['online'] = $ts && ($now - $ts <= XN_DEVICE_OFFLINE_SECONDS);
    if ($d['online']) {
        $online++;
    }
}
unset($d);

include __DIR__ . '/header.php';
?>
<h2>设备管理概览</h2>
<div class="card-grid">
    <div class="card">
        <h3>设备总数</h3>
        <div class="value"><?php echo (int)$total; ?></div>
    </div>
    <div class="card">
        <h3>在线设备</h3>
        <div class="value"><?php echo (int)$online; ?></div>
    </div>
</div>

<div class="table-wrapper">
    <div style="padding: 10px 10px 0 10px;">
        <form method="get" class="search-bar">
            <input type="text" name="q" placeholder="搜索设备 ID 或名称" value="<?php echo htmlspecialchars($q, ENT_QUOTES, 'UTF-8'); ?>">
            <label>排序：
                <select name="sort">
                    <option value="last_seen" <?php echo $sort === 'last_seen' ? 'selected' : ''; ?>>最后在线时间</option>
                    <option value="device_id" <?php echo $sort === 'device_id' ? 'selected' : ''; ?>>设备 ID</option>
                    <option value="created" <?php echo $sort === 'created' ? 'selected' : ''; ?>>创建时间</option>
                </select>
            </label>
            <label>
                <select name="order">
                    <option value="desc" <?php echo $order === 'DESC' ? 'selected' : ''; ?>>降序</option>
                    <option value="asc" <?php echo $order === 'ASC' ? 'selected' : ''; ?>>升序</option>
                </select>
            </label>
            <button type="submit" class="btn">筛选</button>
        </form>
    </div>
    <table>
        <thead>
        <tr>
            <th>设备 ID</th>
            <th>名称</th>
            <th>在线状态</th>
            <th>最后在线时间</th>
            <th>管理模式</th>
            <th>操作</th>
        </tr>
        </thead>
        <tbody>
        <?php if (!$devices): ?>
            <tr><td colspan="6">暂无设备数据，等待 MQTT 心跳上报后自动创建。</td></tr>
        <?php else: ?>
            <?php foreach ($devices as $d): ?>
                <tr>
                    <td><?php echo htmlspecialchars($d['device_id'], ENT_QUOTES, 'UTF-8'); ?></td>
                    <td><?php echo htmlspecialchars($d['name'] ?? '', ENT_QUOTES, 'UTF-8'); ?></td>
                    <td>
                        <?php if ($d['online']): ?>
                            <span class="status-dot status-online"></span>在线
                        <?php else: ?>
                            <span class="status-dot status-offline"></span>离线
                        <?php endif; ?>
                    </td>
                    <td><?php echo $d['last_seen_at'] ? htmlspecialchars($d['last_seen_at'], ENT_QUOTES, 'UTF-8') : '-'; ?></td>
                    <td>
                        <?php if ((int)$d['manage_mode'] === 1): ?>
                            <span class="badge badge-manage-on">管理中</span>
                        <?php else: ?>
                            <span class="badge badge-manage-off">空闲</span>
                        <?php endif; ?>
                    </td>
                    <td>
                        <a class="btn btn-primary" href="device_manage.php?id=<?php echo (int)$d['id']; ?>">设备管理</a>
                    </td>
                </tr>
            <?php endforeach; ?>
        <?php endif; ?>
        </tbody>
    </table>
 </div>

 <?php if ($lastMsg): ?>
     <div style="margin-top: 16px;">
         <div class="card">
             <h3>最近一条 MQTT 消息</h3>
             <div style="font-size: 12px; color: #666; margin-bottom: 6px;">
                 时间：<?php echo htmlspecialchars($lastMsg['created_at'], ENT_QUOTES, 'UTF-8'); ?>
             </div>
             <div style="font-size: 12px; color: #666; margin-bottom: 4px;">
                 Client ID：<?php echo htmlspecialchars($lastMsg['client_id'], ENT_QUOTES, 'UTF-8'); ?>
             </div>
             <div style="font-size: 12px; color: #666; margin-bottom: 4px;">
                 Topic：<?php echo htmlspecialchars($lastMsg['topic'], ENT_QUOTES, 'UTF-8'); ?>
             </div>
             <div style="font-size: 12px; color: #666; margin-top: 4px;">
                 Payload：
                 <pre style="white-space: pre-wrap; word-break: break-all; background:#fafafa; padding:6px; border-radius:4px; border:1px solid #eee; margin:4px 0 0;">
<?php
$payload = (string)($lastMsg['payload'] ?? '');
$display = strlen($payload) > 500 ? substr($payload, 0, 500) . "\n..." : $payload;
echo htmlspecialchars($display, ENT_QUOTES, 'UTF-8');
?>
                 </pre>
             </div>
         </div>
     </div>
 <?php endif; ?>

 <script>
 (function () {
     var REFRESH_INTERVAL_MS = 5000; // 5 秒自动刷新一次
     setInterval(function () {
         if (document.hidden) {
             return;
         }
         window.location.reload();
     }, REFRESH_INTERVAL_MS);
 })();
 </script>

<?php include __DIR__ . '/footer.php'; ?>
