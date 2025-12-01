<?php
require_once __DIR__ . '/config.php';
require_once __DIR__ . '/db.php';

if (session_status() === PHP_SESSION_NONE) {
    session_name(XN_SESSION_NAME);
    session_start();
}

if (isset($_SESSION['xn_user_id'])) {
    header('Location: index.php');
    exit;
}

$error = '';

if ($_SERVER['REQUEST_METHOD'] === 'POST') {
    $username = trim((string)($_POST['username'] ?? ''));
    $password = (string)($_POST['password'] ?? '');

    $db   = xn_get_db();
    $stmt = $db->prepare('SELECT id, password_hash FROM users WHERE username = :u');
    $stmt->execute([':u' => $username]);
    $user = $stmt->fetch();

    if ($user && password_verify($password, $user['password_hash'])) {
        session_regenerate_id(true);
        $_SESSION['xn_user_id'] = $user['id'];
        header('Location: index.php');
        exit;
    } else {
        $error = '用户名或密码错误';
    }
}
?>
<!DOCTYPE html>
<html lang="zh-CN">
<head>
    <meta charset="utf-8">
    <title>MQTT 管理后台登录</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
        body { margin: 0; font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', sans-serif; background: #f5f5f5; }
        .login-wrap { min-height: 100vh; display: flex; align-items: center; justify-content: center; }
        .login-card { background: #fff; padding: 24px 28px; border-radius: 6px; box-shadow: 0 2px 8px rgba(0,0,0,0.15); width: 320px; box-sizing: border-box; }
        h1 { margin: 0 0 16px; font-size: 20px; text-align: center; }
        label { display: block; font-size: 13px; margin-bottom: 4px; }
        input[type="text"], input[type="password"] { width: 100%; padding: 7px 9px; border-radius: 4px; border: 1px solid #ccc; box-sizing: border-box; margin-bottom: 12px; }
        button { width: 100%; padding: 8px 0; border-radius: 4px; border: none; background: #1976d2; color: #fff; font-size: 14px; cursor: pointer; }
        button:hover { background: #1565c0; }
        .error { margin-bottom: 10px; padding: 8px 10px; background: #ffebee; color: #c62828; border-radius: 4px; font-size: 13px; }
        .hint { margin-top: 10px; font-size: 12px; color: #777; }
    </style>
</head>
<body>
<div class="login-wrap">
    <div class="login-card">
        <h1>MQTT 管理后台</h1>
        <?php if ($error): ?>
            <div class="error"><?php echo htmlspecialchars($error, ENT_QUOTES, 'UTF-8'); ?></div>
        <?php endif; ?>
        <form method="post">
            <label for="username">用户名</label>
            <input type="text" id="username" name="username" required>

            <label for="password">密码</label>
            <input type="password" id="password" name="password" required>

            <button type="submit">登录</button>
        </form>
        <div class="hint">首次登录：用户名 admin / 密码 admin123（请登录后及时修改密码）。</div>
    </div>
</div>
</body>
</html>
