# 已自带 / 桩实现 / 明确不在范围内

本文区分三类：**已自带**（开箱即用）、**接口已就绪**（需要业务层组装）、
**故意不做**（按约定走业务 component）。

## 协议层（接口已就绪，全部为真实实现）

| 工厂函数 | 文件 | 状态 |
|----------|------|------|
| `protocol_mqtt_create`       | `components/protocol_service/src/protocol_mqtt.c`       | **真实**（esp-mqtt） |
| `protocol_tcp_client_create` | `components/protocol_service/src/protocol_tcp_client.c` | **真实**（socket + 指数重连） |
| `protocol_tcp_server_create` | `components/protocol_service/src/protocol_tcp_server.c` | **真实**（多客户端广播） |
| `protocol_udp_create`        | `components/protocol_service/src/protocol_udp.c`        | **真实**（bind + 对端回送） |
| `protocol_http_create`       | `components/protocol_service/src/protocol_http.c`       | **真实**（esp_http_client） |

所有协议**只做封装**：`start / stop / send` + RX 回调。
业务策略（何时发、如何编帧、是否回写串口）**不在协议实现内**。

各协议读取的配置键集中于 `components/config_service/include/config_keys.h`：
`CFG_KEY_TCP_*` / `CFG_KEY_TCPS_*` / `CFG_KEY_UDP_*` / `CFG_KEY_HTTP_*` /
`CFG_KEY_MQTT_*`。

## 工作模式（已自带）

`bridge_service` 已提供"通用透传"：串口 ↔ 任一协议双向绑定，
可在 Web `工作模式` 页一键切换。文档参见
[`INTERFACES.md`](INTERFACES.md#bridge_service) 与
[`API.md`](API.md#4-工作模式透传桥)。

如需更复杂的策略（按帧切换、上行模板、心跳格式…），仍然按"新建业务
component" 的方式做，不要把策略塞进 bridge 或协议层。

## 鉴权（已自带）

`auth_service` 提供单用户登录与会话管理。出厂 `admin / admin`，强制改密。

## 网络接入（已自带）

| 能力 | 当前行为 |
|------|----------|
| WiFi STA 重连 | 指数退避（1 s → 60 s 上限） |
| 静态 IP | `/api/network` 设置 + `POST /api/system/reboot`，重启生效 |
| 以太网 | `CFG_KEY_NET_MODE = "eth"` 时由 `net_service` 启用（PHY 通过 menuconfig 选） |
| WiFi 扫描 | `GET /api/network/scan` |
| AP SSID/密码/信道 | `/api/network` 中的 `ap_ssid/ap_password/ap_channel` |

## 故意不在本固件内（属业务 component 范畴）

- 行业协议解析（Modbus-RTU / Modbus-TCP / DLT645 等）
- 平台上报策略（定时 / 触发 / 模板）
- OTA 业务流（断点续传、灰度、双区切换策略）
- WebSocket 客户端 tag 路由
- 多协议并发路由表（同时跑多个协议、按规则分流）

如需以上能力，按 [`DEVELOPMENT_TUTORIAL.md`](DEVELOPMENT_TUTORIAL.md)
的方式新建业务 component，使用 `config_service` + `serial_service` +
`protocol_service` 作为依赖——**禁止**把业务策略塞进接口层。

## 推荐扩展顺序

1. 按 `components/hal_adapters/` 模板加新外设。
2. 复杂策略请新建业务 component（参考 `examples/bridge_serial_tcp_demo/`）；
   "通用透传"不必再写，直接复用 `bridge_service`。
3. 协议增强（鉴权、TLS、心跳格式…）放对应 `protocol_*.c` 内部，不向上层泄漏。
