<?php
require_once __DIR__ . '/auth.php';
xn_require_login();

$db   = xn_get_db();
$user = xn_current_user();

$error   = '';
$success = '';

if ($_SERVER['REQUEST_METHOD'] === 'POST') {
    $old = (string)($_POST['old_password'] ?? '');
    $new = (string)($_POST['new_password'] ?? '');

    $stmt = $db->prepare('SELECT password_hash FROM users WHERE id = :id');
    $stmt->execute([':id' => $user['id']]);
    $row = $stmt->fetch();

    if (!$row || !password_verify($old, $row['password_hash'])) {
        $error = '原密码不正确';
    } elseif (strlen($new) < 6) {
        $error = '新密码长度至少 6 位';
    } else {
        $hash = password_hash($new, PASSWORD_BCRYPT);
        $now  = date('Y-m-d H:i:s');
        $upd  = $db->prepare('UPDATE users SET password_hash = :p, updated_at = :u WHERE id = :id');
        $upd->execute([':p' => $hash, ':u' => $now, ':id' => $user['id']]);
        $success = '密码修改成功，下次登录请使用新密码。';
    }
}

include __DIR__ . '/header.php';
?>
<h2>修改密码</h2>
<?php if ($error): ?>
    <div class="flash flash-error"><?php echo htmlspecialchars($error, ENT_QUOTES, 'UTF-8'); ?></div>
<?php endif; ?>
<?php if ($success): ?>
    <div class="flash flash-success"><?php echo htmlspecialchars($success, ENT_QUOTES, 'UTF-8'); ?></div>
<?php endif; ?>
<form method="post">
    <div>
        <label for="old_password">原密码</label><br>
        <input type="password" id="old_password" name="old_password" required>
    </div>
    <div style="margin-top:8px;">
        <label for="new_password">新密码</label><br>
        <input type="password" id="new_password" name="new_password" required>
    </div>
    <div style="margin-top:12px;">
        <button type="submit" class="btn btn-primary">保存</button>
    </div>
</form>
<?php include __DIR__ . '/footer.php'; ?>
