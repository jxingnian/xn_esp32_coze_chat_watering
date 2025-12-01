<?php
require_once __DIR__ . '/config.php';

/**
 * 获取 PDO 实例（单例），并自动初始化数据库结构
 */
function xn_get_db(): PDO
{
    static $db = null;
    if ($db !== null) {
        return $db;
    }

    $dsn = sprintf(
        'mysql:host=%s;port=%d;dbname=%s;charset=%s',
        XN_DB_HOST,
        XN_DB_PORT,
        XN_DB_NAME,
        XN_DB_CHARSET
    );

    $db = new PDO($dsn, XN_DB_USER, XN_DB_PASS);
    $db->setAttribute(PDO::ATTR_ERRMODE, PDO::ERRMODE_EXCEPTION);
    $db->setAttribute(PDO::ATTR_DEFAULT_FETCH_MODE, PDO::FETCH_ASSOC);

    xn_init_schema($db);

    return $db;
}

/**
 * 初始化数据表及默认管理员账户
 */
function xn_init_schema(PDO $db): void
{
    // 管理员表
    $db->exec('CREATE TABLE IF NOT EXISTS users (
        id INT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
        username VARCHAR(64) NOT NULL UNIQUE,
        password_hash VARCHAR(255) NOT NULL,
        created_at DATETIME NOT NULL,
        updated_at DATETIME NOT NULL
    ) ENGINE=InnoDB DEFAULT CHARSET=' . XN_DB_CHARSET);

    // 设备表
    $db->exec('CREATE TABLE IF NOT EXISTS devices (
        id INT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
        device_id VARCHAR(128) NOT NULL UNIQUE,
        name VARCHAR(255) NULL,
        last_seen_at DATETIME NULL,
        last_ip VARCHAR(64) NULL,
        meta_json TEXT NULL,
        manage_mode TINYINT(1) NOT NULL DEFAULT 0,
        created_at DATETIME NOT NULL,
        updated_at DATETIME NOT NULL
    ) ENGINE=InnoDB DEFAULT CHARSET=' . XN_DB_CHARSET);

    // MQTT 消息表
    $db->exec('CREATE TABLE IF NOT EXISTS mqtt_messages (
        id INT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
        client_id VARCHAR(128) NOT NULL,
        topic VARCHAR(255) NOT NULL,
        payload TEXT NULL,
        created_at DATETIME NOT NULL
    ) ENGINE=InnoDB DEFAULT CHARSET=' . XN_DB_CHARSET);

    // 默认管理员
    $stmt = $db->query('SELECT COUNT(*) AS c FROM users');
    $row  = $stmt->fetch();
    if ((int)$row['c'] === 0) {
        $now  = date('Y-m-d H:i:s');
        $hash = password_hash(XN_DEFAULT_ADMIN_PASS, PASSWORD_BCRYPT);
        $ins  = $db->prepare('INSERT INTO users (username, password_hash, created_at, updated_at)
                              VALUES (:u, :p, :c, :u2)');
        $ins->execute([
            ':u'  => XN_DEFAULT_ADMIN_USER,
            ':p'  => $hash,
            ':c'  => $now,
            ':u2' => $now,
        ]);
    }
}

/**
 * 根据 client_id 获取或创建设备记录
 */
function xn_upsert_device(PDO $db, string $deviceId): array
{
    $now = date('Y-m-d H:i:s');

    $sel = $db->prepare('SELECT * FROM devices WHERE device_id = :id');
    $sel->execute([':id' => $deviceId]);
    $device = $sel->fetch();

    if ($device) {
        $upd = $db->prepare('UPDATE devices SET updated_at = :u WHERE id = :id');
        $upd->execute([':u' => $now, ':id' => $device['id']]);
        $device['updated_at'] = $now;
        return $device;
    }

    $ins = $db->prepare('INSERT INTO devices (device_id, created_at, updated_at)
                         VALUES (:id, :c, :u)');
    $ins->execute([
        ':id' => $deviceId,
        ':c'  => $now,
        ':u'  => $now,
    ]);
    $id = (int)$db->lastInsertId();

    $sel = $db->prepare('SELECT * FROM devices WHERE id = :id');
    $sel->execute([':id' => $id]);
    return $sel->fetch();
}
