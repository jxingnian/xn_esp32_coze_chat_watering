<?php
/**
 * 简单 MQTT 3.1.1 客户端（纯 PHP 实现，支持 QoS 0 发布和基础订阅）。
 *
 * 仅用于网站后台与 EMQX 交互：
 *  - 典型用法：短连接 + publish；
 *  - subscribe 适合在 CLI/守护脚本中使用，不建议在 Web 请求中长时间阻塞。
 */

class XnMqttClient
{
    private string $host;
    private int $port;
    private string $clientId;
    private ?string $username;
    private ?string $password;
    private int $keepAlive;

    /** @var resource|null */
    private $socket = null;
    private bool $connected = false;

    public function __construct(
        string $host,
        int $port = 1883,
        ?string $clientId = null,
        ?string $username = null,
        ?string $password = null,
        int $keepAlive = 60
    ) {
        $this->host     = $host;
        $this->port     = $port;
        $this->clientId = $clientId ?: ('xn_mqtt_php_' . bin2hex(random_bytes(4)));
        $this->username = $username;
        $this->password = $password;
        $this->keepAlive = $keepAlive;
    }

    public function __destruct()
    {
        $this->disconnect();
    }

    /**
     * 连接到 MQTT 服务器（如已连接则直接返回）。
     *
     * @throws RuntimeException 连接失败或 CONNACK 异常
     */
    public function connect(): void
    {
        if ($this->connected) {
            return;
        }

        $errno = 0;
        $errstr = '';
        $this->socket = @fsockopen($this->host, $this->port, $errno, $errstr, 5.0);
        if (!$this->socket) {
            throw new RuntimeException('MQTT connect failed: ' . $errstr . " ($errno)");
        }
        stream_set_timeout($this->socket, 5);

        $packet = $this->buildConnectPacket();
        $this->write($packet);

        $resp = $this->readPacket();
        if ($resp === null || $resp['type'] !== 2) { // 2 = CONNACK
            $this->disconnect();
            throw new RuntimeException('Invalid CONNACK from MQTT server');
        }
        if (strlen($resp['body']) < 2) {
            $this->disconnect();
            throw new RuntimeException('Malformed CONNACK');
        }
        $returnCode = ord($resp['body'][1]);
        if ($returnCode !== 0) {
            $this->disconnect();
            throw new RuntimeException('MQTT CONNACK error code: ' . $returnCode);
        }

        $this->connected = true;
    }

    /**
     * 发布消息（QoS 0）。
     */
    public function publish(string $topic, string $payload, bool $retain = false): void
    {
        if (!$this->connected) {
            $this->connect();
        }

        $fixedHeader = 0x30 | ($retain ? 0x01 : 0x00); // PUBLISH, QoS 0
        $topicBin = $this->encodeString($topic);
        $body = $topicBin . $payload;
        $packet = chr($fixedHeader) . $this->encodeLength(strlen($body)) . $body;

        $this->write($packet);
    }

    /**
     * 简单订阅并处理一段时间内的消息（适合 CLI 脚本）。
     *
     * @param string   $topicFilter  订阅的 Topic 过滤器
     * @param callable $callback     function(string $topic, string $payload): void
     * @param int      $durationSec  处理时长（秒）
     */
    public function subscribeLoop(string $topicFilter, callable $callback, int $durationSec = 30): void
    {
        if (!$this->connected) {
            $this->connect();
        }

        // 发送 SUBSCRIBE（QoS 0），报文标识符固定用 1
        $packetId = 1;
        $topicBin = $this->encodeString($topicFilter) . chr(0x00); // QoS 0
        $body = chr($packetId >> 8) . chr($packetId & 0xFF) . $topicBin;
        $fixedHeader = 0x82; // SUBSCRIBE, QoS 1
        $packet = chr($fixedHeader) . $this->encodeLength(strlen($body)) . $body;
        $this->write($packet);

        // 先读 SUBACK（忽略内容）
        $suback = $this->readPacket();
        if ($suback === null || $suback['type'] !== 9) { // 9 = SUBACK
            throw new RuntimeException('Did not receive SUBACK');
        }

        $endTime = time() + $durationSec;
        while (time() < $endTime) {
            $pkt = $this->readPacket(1.0);
            if ($pkt === null) {
                continue;
            }
            if ($pkt['type'] === 3) { // PUBLISH
                $this->handlePublish($pkt['body'], $callback);
            }
        }
    }

    public function disconnect(): void
    {
        if ($this->socket) {
            // 发送 DISCONNECT 报文（可选）
            @fwrite($this->socket, chr(0xE0) . chr(0x00));
            @fclose($this->socket);
        }
        $this->socket = null;
        $this->connected = false;
    }

    // ---------------- 内部工具方法 ----------------

    private function buildConnectPacket(): string
    {
        $protocolName = $this->encodeString('MQTT');
        $protocolLevel = chr(0x04); // MQTT 3.1.1

        $connectFlags = 0;
        $connectFlags |= 0x02; // Clean Session
        if ($this->username !== null && $this->username !== '') {
            $connectFlags |= 0x80;
        }
        if ($this->password !== null && $this->password !== '') {
            $connectFlags |= 0x40;
        }

        $keepAlive = chr(($this->keepAlive >> 8) & 0xFF) . chr($this->keepAlive & 0xFF);

        $variableHeader = $protocolName . $protocolLevel . chr($connectFlags) . $keepAlive;

        $payload = $this->encodeString($this->clientId);
        if ($this->username !== null && $this->username !== '') {
            $payload .= $this->encodeString($this->username);
        }
        if ($this->password !== null && $this->password !== '') {
            $payload .= $this->encodeString($this->password);
        }

        $body = $variableHeader . $payload;
        $fixedHeader = chr(0x10) . $this->encodeLength(strlen($body));

        return $fixedHeader . $body;
    }

    private function encodeString(string $str): string
    {
        $len = strlen($str);
        return chr(($len >> 8) & 0xFF) . chr($len & 0xFF) . $str;
    }

    private function encodeLength(int $length): string
    {
        $encoded = '';
        do {
            $digit = $length % 128;
            $length = intdiv($length, 128);
            if ($length > 0) {
                $digit |= 0x80;
            }
            $encoded .= chr($digit);
        } while ($length > 0);
        return $encoded;
    }

    /**
     * 读取一个 MQTT 报文，返回 [type, flags, body]，或在超时时返回 null。
     */
    private function readPacket(float $timeoutSec = 5.0): ?array
    {
        if (!$this->socket) {
            return null;
        }
        $sec = (int)$timeoutSec;
        $usec = (int)(($timeoutSec - $sec) * 1_000_000);
        stream_set_timeout($this->socket, $sec, $usec);

        $header = @fread($this->socket, 1);
        if ($header === '' || $header === false) {
            $meta = stream_get_meta_data($this->socket);
            if (!empty($meta['timed_out'])) {
                return null;
            }
            return null;
        }
        $byte1 = ord($header);
        $type = $byte1 >> 4;
        $flags = $byte1 & 0x0F;

        // Remaining Length (可变长度编码)
        $multiplier = 1;
        $value = 0;
        do {
            $ch = @fread($this->socket, 1);
            if ($ch === '' || $ch === false) {
                return null;
            }
            $digit = ord($ch);
            $value += ($digit & 127) * $multiplier;
            $multiplier *= 128;
        } while (($digit & 128) !== 0 && $multiplier <= 128 * 128 * 128);

        $body = '';
        $remaining = $value;
        while ($remaining > 0) {
            $chunk = @fread($this->socket, $remaining);
            if ($chunk === '' || $chunk === false) {
                break;
            }
            $body .= $chunk;
            $remaining -= strlen($chunk);
        }

        return [
            'type'  => $type,
            'flags' => $flags,
            'body'  => $body,
        ];
    }

    /**
     * 处理 PUBLISH 报文体并调用回调。
     */
    private function handlePublish(string $body, callable $callback): void
    {
        if (strlen($body) < 2) {
            return;
        }
        $len = (ord($body[0]) << 8) + ord($body[1]);
        if (strlen($body) < 2 + $len) {
            return;
        }
        $topic = substr($body, 2, $len);
        $payload = substr($body, 2 + $len);
        $callback($topic, $payload);
    }

    private function write(string $data): void
    {
        if (!$this->socket) {
            throw new RuntimeException('MQTT socket is not connected');
        }
        $len = strlen($data);
        $written = 0;
        while ($written < $len) {
            $n = @fwrite($this->socket, substr($data, $written));
            if ($n === false || $n === 0) {
                throw new RuntimeException('Failed to write to MQTT socket');
            }
            $written += $n;
        }
    }
}
