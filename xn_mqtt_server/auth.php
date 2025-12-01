<?php
require_once __DIR__ . '/config.php';
require_once __DIR__ . '/db.php';

if (session_status() === PHP_SESSION_NONE) {
    session_name(XN_SESSION_NAME);
    session_start();
}

function xn_is_logged_in(): bool
{
    return isset($_SESSION['xn_user_id']);
}

function xn_require_login(): void
{
    if (!xn_is_logged_in()) {
        header('Location: login.php');
        exit;
    }
}

function xn_current_user(): ?array
{
    static $user = null;

    if (!xn_is_logged_in()) {
        return null;
    }

    if ($user !== null) {
        return $user;
    }

    $db   = xn_get_db();
    $stmt = $db->prepare('SELECT id, username, created_at, updated_at FROM users WHERE id = :id');
    $stmt->execute([':id' => $_SESSION['xn_user_id']]);
    $user = $stmt->fetch();

    if (!$user) {
        session_destroy();
        return null;
    }

    return $user;
}
