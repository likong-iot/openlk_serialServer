# Web API 契约

所有响应统一信封：

```jsonc
{
  "code": 0,              // 0 = 成功；其它见下表
  "msg":  "ok",           // 人类可读
  "data": { /* … */ }     // 业务数据，错误时为 null
}
```

## 鉴权与传输

- 出厂默认账号：`admin / admin`，首次登录被强制改密。
- 受保护接口必须携带 `Authorization: Bearer <token>`，token 由
  `POST /api/auth/login` 颁发，存活在内存中（重启即失效），
  滑动 30 分钟过期。
- 静态资源 (`/`, `/app.*`, `/modules/*`, `/pages/*`) 与 `/api/auth/login`、
  `/api/auth/logout` **不需要** token。
- WebSocket 通过 `?token=<token>` 查询参数携带。
- token 失效时 API 返回 `code = 401`，前端会清除本地 token 并跳转
  登录页。

## 统一错误码

| code | 含义 |
|-----:|------|
|   0  | 成功 |
| 400  | 参数错误 |
| 401  | 未鉴权（无 token / token 失效 / 凭据错误） |
| 404  | 路由不存在 |
| 409  | 状态错误（如串口未启动、扫描忙） |
| 500  | 内部错误 |
| 501  | 功能未实现（桩） |

---

## 1. 鉴权

### POST `/api/auth/login`

请求：

```jsonc
{ "user": "admin", "password": "admin" }
```

响应 `data`：

```jsonc
{
  "token":       "f2a1…(32 hex)",   // 32 字节 hex 随机串
  "must_change": true               // 当前账号是否仍是出厂默认
}
```

凭据错误返回 `code = 401`。

### POST `/api/auth/logout`

请求体可为空。需带 `Authorization: Bearer …` 头；服务端把对应
token 从会话表删除。**总是**返回成功，方便前端无脑调用。

### GET `/api/auth/me`

```jsonc
{ "user": "admin", "must_change": false }
```

### POST `/api/auth/change_password`

```jsonc
{ "old": "admin", "new": "newP@ss" }
```

约束：

- 必须携带有效 token；
- `old` 必须与当前持久化哈希匹配；
- `new` 长度 4–63；
- 修改成功后 `must_change` 自动清零。

---

## 2. 配网

### GET `/api/network`

```jsonc
{
  "mode":       "wifi",        // "wifi" | "eth"
  "dhcp":       true,
  "ssid":       "home-wifi",
  "ip":         "192.168.1.100",
  "mask":       "255.255.255.0",
  "gateway":    "192.168.1.1",
  "ap_ssid":    "Gateway-1A2B3C",
  "ap_channel": 1
}
```

> 出于安全考虑，密码字段（WiFi STA 密码、AP 密码）**永远不返回**。

### POST `/api/network`

请求体（所有字段可选；缺省项保持原值）：

```jsonc
{
  "mode":        "wifi",
  "dhcp":        true,
  "ssid":        "home-wifi",
  "password":    "…",
  "ip":          "192.168.1.100",
  "mask":        "255.255.255.0",
  "gateway":     "192.168.1.1",
  "ap_ssid":     "MyGateway",
  "ap_password": "12345678",
  "ap_channel":  6
}
```

响应：

```jsonc
{ "applied": true, "reboot_required": true }
```

约束：

- `mode` 仅 `wifi` / `eth`
- `ip / mask / gateway` 若非空必须合法 IPv4
- `ap_ssid` 长度 1–32
- `ap_password` 长度 8–63 或为空（空 = 不修改）
- `ap_channel` ∈ [1, 13]
- 任意密码字段为空字符串均表示"保留旧值"

### GET `/api/network/scan`

同步扫描周围 AP（约 1–2 秒），按 SSID 去重（同名取最强）。

```jsonc
{
  "count": 3,
  "aps": [
    { "ssid": "home-wifi", "rssi": -42, "channel": 6, "auth": "wpa2",  "open": false },
    { "ssid": "office",    "rssi": -67, "channel": 1, "auth": "wpa/2", "open": false },
    { "ssid": "guest",     "rssi": -78, "channel": 11,"auth": "open",  "open": true  }
  ]
}
```

`auth` 取值：`open / wep / wpa / wpa2 / wpa/2 / wpa2-ent / wpa3 / wpa2/3 / ?`。

错误：`409` = 扫描忙（上一次扫描尚未结束）。

---

## 3. 串口

### GET `/api/serial`

```jsonc
{
  "baud":         115200,
  "data_bits":    8,
  "stop_bits":    1,        // 1 或 3（3 = 1.5 / 2.0 停止位）
  "parity":       "none",   // "none" | "even" | "odd"
  "flow_ctrl":    false,
  "rs485":        false,
  "frame_gap_ms": 0          // 0 = 按波特率自动
}
```

### POST `/api/serial`

字段同上，所有字段可选。响应：`{ "applied": true }`。

约束：

- `baud` ∈ [1200, 921600]
- `data_bits` ∈ [5, 8]

### POST `/api/serial/send`

```jsonc
{ "fmt": "hex" | "text", "data": "01 02 AF" }
```

响应 `{ "sent": 3 }`。`hex` 模式忽略空白和 `0x` 前缀。

### WebSocket `/ws/serial?token=<token>`

服务端推送：

```jsonc
{
  "dir":  "rx",
  "ts":   1713300000000,
  "fmt":  "hex",
  "data": "01 03 02 00 64 B8 45"
}
```

> 当前 WS 单向推送；发送一律走 `POST /api/serial/send`，方便后续支持
> 大包分片时直接在帧中追加 `seq / total`。
> 没有有效 token 时握手会被拒绝（HTTP 401）。

---

## 4. 工作模式（透传桥）

### GET `/api/workmode`

```jsonc
{
  "mode":  "tcp_client",     // 当前模式
  "modes": ["off","tcp_client","tcp_server","udp","mqtt","http"],

  "tcp_client": { "host":"192.168.1.100", "port":9000, "reconn_ms":2000 },
  "tcp_server": { "port":8080,  "max_clients":4 },
  "udp":        { "local_port":9000, "remote_host":"192.168.1.100", "remote_port":9001 },
  "mqtt": {
    "uri":       "mqtt://broker.emqx.io:1883",
    "client_id": "sp501lw-001",
    "user":      "device",
    "pub_topic": "device/up",
    "sub_topic": "device/down",
    "qos":       0
  },
  "http": { "url":"http://host/path", "method":"POST", "timeout_ms":5000 },

  "status": {
    "mode":       "tcp_client",
    "state":      "connected",      // stopped / starting / connected / disconnected / error
    "tx_bytes":   3072,             // 串口 → 协议
    "rx_bytes":   1024,             // 协议 → 串口
    "tx_packets": 12,
    "rx_packets": 8,
    "last_error": 0,
    "started_ms": 423199
  }
}
```

> MQTT `password` 永远不返回，跟 WiFi 密码同样的保护策略。

### POST `/api/workmode`

请求体：必带 `mode`；按需带对应模式的参数块。常见做法是只发 `mode` 和当前
所选模式那一个块。

```jsonc
{
  "mode": "tcp_client",
  "tcp_client": { "host":"192.168.1.100", "port":9000, "reconn_ms":2000 }
}
```

```jsonc
{
  "mode": "mqtt",
  "mqtt": {
    "uri":      "mqtt://broker.emqx.io:1883",
    "client_id":"sp501lw-001",
    "user":     "device",
    "password": "secret",      // 留空 = 不修改
    "pub_topic":"device/up",
    "sub_topic":"device/down",
    "qos":      0
  }
}
```

```jsonc
{ "mode": "off" }
```

响应：

```jsonc
{
  "status": { /* 同上 status 结构 */ }
}
```

约束（与协议层保持一致）：

- `tcp_client.port`、`tcp_server.port`、`udp.local_port`、`udp.remote_port`
  ∈ [1, 65535]
- `tcp_client.reconn_ms` ∈ [200, 60000]
- `tcp_server.max_clients` ∈ [1, 16]
- `http.method` 仅 `GET` / `POST`
- `http.timeout_ms` ∈ [100, 60000]
- `mqtt.qos` ∈ [0, 2]
- `mqtt.password` 留空 = 保留旧值

### GET `/api/workmode/status`

只返回上面 `status` 子对象（轻量端点，可作 1–2 Hz 轮询）。

---

## 5. 系统

### GET `/api/system/status`

```jsonc
{
  "net": {
    "mode":     "wifi",
    "link_up":  true,
    "wifi_up":  true,
    "got_ip":   true,
    "ssid":     "home-wifi",
    "rssi":     -52,
    "ip":       "192.168.1.120",
    "mask":     "255.255.255.0",
    "gateway":  "192.168.1.1"
  },
  "sys": {
    "uptime_ms":     342100,
    "free_heap":     182340,
    "min_free_heap": 171200
  },
  "fw": {
    "version":      "1.1.0",
    "idf_version":  "v5.5",
    "project_name": "SP501LW",
    "compile_date": "May  7 2026",
    "compile_time": "12:34:56"
  },
  "hw": { "cores": 2, "revision": 3 }
}
```

### GET `/api/system/info`

聚合给"基本信息"页用的更详细版本：

```jsonc
{
  "fw": {
    "version":      "1.1.0",
    "project":      "SP501LW",
    "compile_date": "May  7 2026",
    "compile_time": "12:34:56",
    "idf_version":  "v5.5"
  },
  "hw": {
    "model":     "ESP32",
    "cores":     2,
    "revision":  3,
    "flash_mb":  2,
    "mac_sta":   "AC:67:B2:1A:2B:3C",
    "mac_ap":    "AC:67:B2:1A:2B:3D"
  },
  "sys": {
    "uptime_ms":     342100,
    "free_heap":     182340,
    "min_free_heap": 171200,
    "psram_total":   0,
    "psram_free":    0,
    "reset_reason":  "power-on"   // power-on/external/software/panic/...
  },
  "net": { /* 与 /status 中 net 字段一致 */ }
}
```

### POST `/api/system/reboot`

请求体 `{}`（保留结构以便后续扩展）。响应：

```jsonc
{ "delay_ms": 800 }
```

响应返回后约 `delay_ms` 毫秒内设备软复位。

---

## 可扩展点

1. 新字段遵循"可选、向后兼容默认值"原则。
2. 新错误码在 `web_internal.h` 定义，并同步本文档。
3. 新增受保护接口时务必在 handler 第一行调用
   `web_auth_require(req)`。
