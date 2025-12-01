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
        body { margin: 0; font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', sans-serif; background: #f3f4f6; color:#222; }
        a { color: #1976d2; text-decoration: none; }
        a:hover { text-decoration: underline; }
        .layout { min-height: 100vh; display: flex; flex-direction: column; }
        .header { background: linear-gradient(90deg,#1976d2,#2196f3); color: #fff; padding: 10px 20px; display: flex; align-items: center; justify-content: space-between; box-shadow: 0 2px 6px rgba(0,0,0,0.15); }
        .logo { font-weight: 600; letter-spacing: 0.5px; }
        .nav a { margin-right: 15px; color: #fff; font-size: 13px; }
        .main { flex: 1; padding: 20px; max-width: 1100px; margin: 0 auto; box-sizing: border-box; }
        .footer { text-align: center; padding: 10px 0 20px; font-size: 12px; color: #888; }

        .page-title { margin: 0 0 12px; font-size: 20px; font-weight: 600; color: #111827; }
        .section { margin-top: 18px; }
        .section-title { margin: 0 0 8px; font-size: 15px; font-weight: 600; color: #374151; }
        .section-subtitle { margin: 0 0 10px; font-size: 13px; color: #6b7280; }

        .card-grid { display: flex; flex-wrap: wrap; gap: 16px; margin-bottom: 20px; }
        .card { background: #fff; padding: 16px; border-radius: 8px; box-shadow: 0 1px 4px rgba(15,23,42,0.08); flex: 1; min-width: 260px; box-sizing:border-box; }
        .card h3 { margin: 0 0 8px; font-size: 14px; color: #4b5563; }
        .card .value { font-size: 24px; font-weight: 600; }

        .table-wrapper { background: #fff; border-radius: 8px; box-shadow: 0 1px 4px rgba(15,23,42,0.08); overflow: hidden; }
        table { border-collapse: collapse; width: 100%; font-size: 13px; }
        th, td { padding: 8px 10px; border-bottom: 1px solid #edf2f7; text-align: left; }
        th { background: #f9fafb; font-weight: 500; color:#4b5563; }
        tr:nth-child(even) td { background: #f9fafb; }

        .status-dot { display: inline-block; width: 9px; height: 9px; border-radius: 50%; margin-right: 4px; }
        .status-online { background: #22c55e; }
        .status-offline { background: #9ca3af; }

        .badge { display: inline-block; padding: 2px 6px; border-radius: 999px; font-size: 11px; }
        .badge-manage-on { background: #ff9800; color: #fff; }
        .badge-manage-off { background: #e5e7eb; color: #4b5563; }

        .btn { display: inline-block; padding: 6px 12px; font-size: 13px; border-radius: 999px; border: 1px solid #1976d2; color: #1976d2; background: #fff; cursor: pointer; transition: background 0.15s, color 0.15s, box-shadow 0.15s; }
        .btn-primary { background: #1976d2; color: #fff; }
        .btn-danger { border-color: #d32f2f; color: #d32f2f; }
        .btn:hover { box-shadow: 0 1px 3px rgba(15,23,42,0.25); }
        .btn + .btn { margin-left: 6px; }

        .search-bar { margin-bottom: 10px; display: flex; flex-wrap: wrap; gap: 8px; align-items: center; }
        .search-bar input[type="text"] { padding: 5px 8px; border-radius: 4px; border: 1px solid #d1d5db; min-width: 200px; }
        .search-bar select { padding: 5px 8px; border-radius: 4px; border: 1px solid #d1d5db; }

        .flash { padding: 10px 12px; border-radius: 6px; margin-bottom: 12px; font-size: 13px; }
        .flash-error { background: #fef2f2; color: #b91c1c; }
        .flash-success { background: #ecfdf3; color: #15803d; }

        .input { padding: 5px 8px; border-radius: 4px; border: 1px solid #d1d5db; font-size: 13px; box-sizing:border-box; }
        .form-group { margin-bottom: 6px; }
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
