# Web API Contract (Minimal)

所有响应统一信封：

```jsonc
{
  "code": 0,              // 0 = ok；其它参见下表
  "msg":  "ok",           // 人类可读信息
  "data": { /* … */ }     // 业务数据，错误时为 null
}
```

## 统一错误码

| code | 含义 |
|-----:|------|
|   0  | 成功 |
| 400  | 参数错误 |
| 404  | 路由不存在 |
| 409  | 状态错误（如串口未启动） |
| 500  | 内部错误 |
| 501  | 功能未实现（桩） |

---

## 1. 配网

### GET `/api/network`

响应 `data`：

```jsonc
{
  "mode":    "wifi" | "eth",
  "dhcp":    true,
  "ssid":    "home-wifi",
  "ip":      "192.168.1.100",
  "mask":    "255.255.255.0",
  "gateway": "192.168.1.1"
}
```

### POST `/api/network`

请求体（所有字段可选；未提供则保留原值）：

```jsonc
{
  "mode":    "wifi",
  "dhcp":    true,
  "ssid":    "home-wifi",
  "password":"…",      // 留空不覆盖
  "ip":      "192.168.1.100",
  "mask":    "255.255.255.0",
  "gateway": "192.168.1.1"
}
```

响应 `data`：

```jsonc
{ "applied": true, "reboot_required": true }
```

约束：
- `mode` 只能是 `wifi` 或 `eth`
- `ip / mask / gateway` 若提供且非空，必须是合法 IPv4 文本
- `password` 为空字符串时不覆盖已保存密码

---

## 2. 配串口

### GET `/api/serial`

```jsonc
{
  "baud":          115200,
  "data_bits":     8,
  "stop_bits":     1,        // 1 或 3（2.0 停止位）
  "parity":        "none",   // "none" | "even" | "odd"
  "flow_ctrl":     false,
  "rs485":         false,
  "frame_gap_ms":  0          // 0 = 按波特率自动
}
```

### POST `/api/serial`

响应：`{ "applied": true }`。

约束：
- `baud` ∈ `[1200, 921600]`
- `data_bits` ∈ `[5, 8]`

---

## 3. 串口调试

### POST `/api/serial/send`

请求：

```jsonc
{ "fmt": "hex" | "text", "data": "01 02 AF" }
```

响应：

```jsonc
{ "sent": 3 }
```

### WebSocket `/ws/serial`

服务端推送：

```jsonc
{
  "dir":  "rx",
  "ts":   1713300000000,
  "fmt":  "hex",
  "data": "01 03 02 00 64 B8 45"
}
```

> 发送请使用 REST `POST /api/serial/send`。
> 当前 WebSocket 为单向推送；后续若支持大包分片，在 frame 中追加 `seq` / `total`。

---

## 4. WiFi 扫描

### GET `/api/network/scan`

同步扫描周围 AP（大约 1–2 秒）。返回按 SSID 去重后的列表（同名取最强信号）。

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

错误：
- `409`：扫描忙（如上一次扫描尚未结束）

---

## 5. 系统

### GET `/api/system/status`

```jsonc
{
  "net": {
    "mode":     "wifi",
    "link_up":  true,      // 通用链路状态：WiFi已关联 或 以太网链路up
    "wifi_up":  true,
    "got_ip":   true,
    "ssid":     "home-wifi",
    "rssi":     -52,
    "ip":       "192.168.1.120",
    "mask":     "255.255.255.0",
    "gateway":  "192.168.1.1"
  },
  "sys": {
    "uptime_ms":      342100,
    "free_heap":      182340,
    "min_free_heap":  171200
  },
  "fw": {
    "version":       "1.1.0",
    "idf_version":   "v5.2",
    "project_name":  "SP501LW",
    "compile_date":  "Apr 17 2026",
    "compile_time":  "12:34:56"
  },
  "hw": { "cores": 2, "revision": 3 }
}
```

### POST `/api/system/reboot`

请求体 `{}`（保留结构，暂无参数）。响应：

```jsonc
{ "delay_ms": 800 }
```

响应返回后 `delay_ms` 毫秒设备软复位。

---

## 可扩展点

1. 新字段遵循"可选、兼容默认值"原则。
2. 新错误码在 `web_internal.h` 定义，并同步本文档。
