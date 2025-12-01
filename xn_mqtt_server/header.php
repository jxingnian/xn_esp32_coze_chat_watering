<?php
require_once __DIR__ . '/auth.php';
$user = xn_current_user();
?>
<!DOCTYPE html>
<html lang="zh-CN">
<head>
    <meta charset="utf-8">
    <title>MQTT 管理后台</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
        body { margin: 0; font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', sans-serif; background: #f5f5f5; }
        a { color: #1976d2; text-decoration: none; }
        a:hover { text-decoration: underline; }
        .layout { min-height: 100vh; display: flex; flex-direction: column; }
        .header { background: #1976d2; color: #fff; padding: 10px 20px; display: flex; align-items: center; justify-content: space-between; }
        .logo { font-weight: 600; }
        .nav a { margin-right: 15px; color: #fff; }
        .main { flex: 1; padding: 20px; max-width: 1100px; margin: 0 auto; box-sizing: border-box; }
        .footer { text-align: center; padding: 10px 0 20px; font-size: 12px; color: #888; }
        .card-grid { display: flex; flex-wrap: wrap; gap: 16px; margin-bottom: 20px; }
        .card { background: #fff; padding: 16px; border-radius: 6px; box-shadow: 0 1px 3px rgba(0,0,0,0.08); flex: 1; min-width: 220px; }
        .card h3 { margin: 0 0 8px; font-size: 14px; color: #555; }
        .card .value { font-size: 24px; font-weight: 600; }
        .table-wrapper { background: #fff; border-radius: 6px; box-shadow: 0 1px 3px rgba(0,0,0,0.08); overflow: hidden; }
        table { border-collapse: collapse; width: 100%; font-size: 13px; }
        th, td { padding: 8px 10px; border-bottom: 1px solid #eee; text-align: left; }
        th { background: #fafafa; font-weight: 500; }
        tr:nth-child(even) td { background: #fcfcfc; }
        .status-dot { display: inline-block; width: 9px; height: 9px; border-radius: 50%; margin-right: 4px; }
        .status-online { background: #4caf50; }
        .status-offline { background: #ccc; }
        .badge { display: inline-block; padding: 2px 6px; border-radius: 4px; font-size: 11px; }
        .badge-manage-on { background: #ff9800; color: #fff; }
        .badge-manage-off { background: #e0e0e0; color: #555; }
        .btn { display: inline-block; padding: 5px 10px; font-size: 13px; border-radius: 4px; border: 1px solid #1976d2; color: #1976d2; background: #fff; cursor: pointer; }
        .btn-primary { background: #1976d2; color: #fff; }
        .btn-danger { border-color: #d32f2f; color: #d32f2f; }
        .btn + .btn { margin-left: 6px; }
        .search-bar { margin-bottom: 10px; display: flex; flex-wrap: wrap; gap: 8px; align-items: center; }
        .search-bar input[type="text"] { padding: 5px 8px; border-radius: 4px; border: 1px solid #ccc; min-width: 200px; }
        .search-bar select { padding: 5px 8px; border-radius: 4px; border: 1px solid #ccc; }
        .flash { padding: 10px 12px; border-radius: 4px; margin-bottom: 12px; font-size: 13px; }
        .flash-error { background: #ffebee; color: #c62828; }
        .flash-success { background: #e8f5e9; color: #2e7d32; }
    </style>
</head>
<body>
<div class="layout">
    <header class="header">
        <div class="logo">MQTT 管理后台</div>
        <?php if ($user): ?>
        <nav class="nav">
            <a href="index.php">首页</a>
            <a href="change_password.php">修改密码</a>
            <a href="logout.php">退出</a>
        </nav>
        <div class="user">当前用户：<?php echo htmlspecialchars($user['username'], ENT_QUOTES, 'UTF-8'); ?></div>
        <?php endif; ?>
    </header>
    <main class="main">
