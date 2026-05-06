# 服务接口速查

一张表看完所有服务的入口，详情请看对应头文件。

## config_service

`components/config_service/include/config_service.h`

| 函数 | 作用 |
|------|------|
| `config_service_init / _deinit`               | 初始化 NVS |
| `config_get_str/int/bool/blob(key, out, def)` | 读取，缺失时填默认 |
| `config_set_str/int/bool/blob(key, v)`        | 暂存（待 commit） |
| `config_commit()`                             | 落盘 |
| `config_erase(key)`                           | 删除单键 |
| `config_reset_defaults()`                     | 清空命名空间 |
| `config_validate_int_range(v, min, max)`      | 简单整数范围校验 |

键集中定义于 `config_keys.h`。**禁止任何调用方内联字符串字面量**。

## serial_service

`components/serial_service/include/serial_service.h`

| 函数 | 作用 |
|------|------|
| `serial_service_init / _deinit`          | 创建内部状态（不打开 UART） |
| `serial_service_start / _stop`           | 打开/关闭 UART + RX 任务 |
| `serial_service_configure(cfg)`          | 持久化并热重启生效 |
| `serial_service_get_config(out)`         | 取当前参数 |
| `serial_service_send(data, len)`         | 同步发送 |
| `serial_service_register_rx_cb(cb, arg)` | 注册接收回调（最多 4 路） |
| `serial_service_unregister_rx_cb(cb)`    | 卸载回调 |
| `serial_service_get_status(out)`         | 收发字节、错误码 |

**回调在串口 RX 任务上下文执行**，必须短时返回，不可做阻塞 I/O。

## protocol_service

`components/protocol_service/include/protocol_service.h`

统一接口 + 多种工厂：

```c
protocol_handle_t *h = protocol_mqtt_create();
protocol_register_event_cb(h, on_state, NULL);
protocol_register_rx_cb   (h, on_rx,    NULL);
protocol_start(h);
protocol_send(h, buf, len);
protocol_stop(h);
protocol_destroy(h);
```

五种协议**均为真实实现**：

| 工厂 | 后端 |
|------|------|
| `protocol_mqtt_create`       | esp-mqtt |
| `protocol_tcp_client_create` | socket + 指数重连 |
| `protocol_tcp_server_create` | 多客户端广播 |
| `protocol_udp_create`        | bind + 对端回送 |
| `protocol_http_create`       | esp_http_client |

每个协议从 `config_service` 读取自己的键（`CFG_KEY_TCP_*` / `CFG_KEY_TCPS_*` /
`CFG_KEY_UDP_*` / `CFG_KEY_MQTT_*` / `CFG_KEY_HTTP_*`），调用方不传原始参数。

> 接入新协议：复制 `protocol_stub_common.h`，替换 vtable 即可。
> 业务策略（何时发、如何编帧、转发到串口）**不属于**协议实现，请放业务 component。

## bridge_service

`components/bridge_service/include/bridge_service.h`

把 `serial_service` 与一个 `protocol_handle_t` 绑成"工作模式"的双向桥：

| 函数 | 作用 |
|------|------|
| `bridge_service_init()`                    | 读取持久化模式，订阅串口 RX；不启动协议 |
| `bridge_service_start()`                   | 按当前模式启动协议 |
| `bridge_service_stop()`                    | 关闭并销毁当前协议 |
| `bridge_service_apply_mode(mode)`          | 持久化新模式并重启桥（参数请先写 `config_service`） |
| `bridge_service_get_status(out)`           | 状态、收发字节/包数、最近错误 |
| `bridge_mode_str(m) / bridge_mode_from(s)` | 模式名互转 |

模式枚举：

```c
BRIDGE_MODE_OFF
BRIDGE_MODE_TCP_CLIENT
BRIDGE_MODE_TCP_SERVER
BRIDGE_MODE_UDP
BRIDGE_MODE_MQTT
BRIDGE_MODE_HTTP
```

数据流向：

```
serial RX  → protocol_send  (上行)
protocol RX → serial_service_send (下行)
```

线程安全；模式切换会先 detach 协议回调，再 stop + destroy，避免悬挂回调访问已释放句柄。

## auth_service

`components/auth_service/include/auth_service.h`

| 函数 | 作用 |
|------|------|
| `auth_service_init()`                                | 出厂注入 admin/admin（mustchg=true） |
| `auth_service_login(user, pass, out_token, &mustchg)`| 校验+发 token；token 长 32 hex |
| `auth_service_logout(token)`                         | 销毁会话；幂等 |
| `auth_service_session_valid(token)`                  | 校验并刷新滑动 TTL |
| `auth_service_whoami(token, out, sz, &mustchg)`      | 从 token 反查账号 |
| `auth_service_change_password(token, old, new)`      | 校验旧密码、写新哈希、清 mustchg |

设计要点：

- 持久化只存 SHA-256(salt‖密码) 与随机 16 字节 salt，**不存明文**。
- 会话 token 在内存表，最多 4 路并发；重启后全部失效（这是有意为之，
  避免 token 泄露后无法回收）。
- 滑动过期 30 分钟，每次校验后刷新。
- 比较口令哈希用常时间比较，规避旁路时序。

## net_service

`components/net_service/include/net_service.h`

| 函数 | 作用 |
|------|------|
| `net_service_init()`              | 启动 TCP/IP、WiFi AP+STA、应用静态 IP、事件驱动重连 |
| `net_service_get_status(out)`     | 当前 SSID / RSSI / IP / uptime / heap |
| `net_service_scan(buf, cap, *n)`  | 同步扫描 AP，去重 SSID |
| `net_service_request_reboot(ms)`  | 延迟软复位 |

## hal_adapters

- `hal_gpio.h` —— 引脚配置 / 读写 / 中断注册
- `hal_uart.h` —— driver_install / 读写 / 查询可读字节（仅供 `serial_service` 用）
- `hal_timer.h` —— `esp_timer` 薄封装，周期 / 单次定时器

## web_minimal

`components/web_minimal/include/web_minimal.h`

```c
web_minimal_config_t c = { .port = 80, .max_sockets = 7 };
web_minimal_start(&c);
```

启动后自动注册：

- 静态 SPA：`/`, `/app.css`, `/app.js`, `/modules/*.js`, `/pages/*.js`
- 鉴权：`POST /api/auth/{login,logout,change_password}`，`GET /api/auth/me`
- 配网：`GET/POST /api/network`，`GET /api/network/scan`
- 串口：`GET/POST /api/serial`，`POST /api/serial/send`
- 工作模式：`GET/POST /api/workmode`，`GET /api/workmode/status`
- 系统：`GET /api/system/{status,info}`，`POST /api/system/reboot`
- WebSocket：`/ws/serial?token=…`
- Captive portal 探测路径：`/connecttest.txt`、`/ncsi.txt`、`/hotspot-detect.html`、`/generate_204` 等

接口除登录、登出与静态资源外，全部由 `web_auth_require()` 守护。
