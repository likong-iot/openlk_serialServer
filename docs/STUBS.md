# 桩实现与未实现业务清单

本版（v1.1）定位为"基础分发固件"，以下模块**仅提供接口**，不含业务逻辑。

## 协议服务

| 工厂函数 | 文件 | 状态 |
|----------|------|------|
| `protocol_mqtt_create`       | `components/protocol_service/src/protocol_mqtt.c`       | **真实**（esp-mqtt） |
| `protocol_tcp_client_create` | `components/protocol_service/src/protocol_tcp_client.c` | **真实**（socket + 指数重连） |
| `protocol_tcp_server_create` | `components/protocol_service/src/protocol_tcp_server.c` | **真实**（多客户端广播） |
| `protocol_udp_create`        | `components/protocol_service/src/protocol_udp.c`        | **真实**（bind + 对端回送） |
| `protocol_http_create`       | `components/protocol_service/src/protocol_http.c`       | **真实**（esp_http_client） |

所有协议一律**只做封装**：`start/stop/send` + rx 回调。
业务策略（何时发、如何转发到 UART、如何编帧）**不在协议实现内**，必须由新增的 bridge 组件承担。

各协议读取的配置键集中在 `components/config_service/include/config_keys.h`：
`CFG_KEY_TCP_*` / `CFG_KEY_TCPS_*` / `CFG_KEY_UDP_*` / `CFG_KEY_HTTP_*` / `CFG_KEY_MQTT_*`。

## 网络接入

| 能力 | 当前行为 |
|------|----------|
| WiFi STA 重连 | 指数退避（1s → 60s 上限），在 `net_service.c` 中实现 |
| 静态 IP | `/api/network` 设置 + `POST /api/system/reboot`，重启后生效 |
| 以太网 | `CFG_KEY_NET_MODE="eth"` 时由 `net_service` 启用（`ethernet_init` 依赖 menuconfig 选择 PHY） |
| WiFi 扫描 | `GET /api/network/scan` 已支持 |

## 设备业务（明确不在本版内）

- 工作模式编排（透传 / Modbus 网关）
- 平台上报策略（定时/触发/模板）
- Modbus-RTU / Modbus-TCP 解析
- OTA 业务流
- WebSocket 客户端 tag 路由
- 多协议并发路由表

如需接入，新建业务 component，使用 `config_service` + `serial_service` + `protocol_service` 作为依赖——**禁止**把业务策略塞进接口层。

## 后续建议顺序

1. 按 `components/hal_adapters/` 模板加新外设。
2. 新增 bridge component：把 `serial_service` 的 rx 转发到某个 `protocol_handle_t`；把某个协议的 rx 写回 UART。业务放这里，不要改接口。
3. 新增业务 component 承载工作模式/上报策略等。
4. 若协议实现需要对端鉴权、重连策略等增强，在对应 `protocol_*.c` 内部做，不要泄漏到上层。
