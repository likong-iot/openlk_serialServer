# openlk — SP501LW 串口服务器固件

面向 **立控电子 SP501LW** 串口网关、以及任何引脚兼容的 ESP32 串口转网络设备的
开源固件。

硬件页：<https://docv2.likong-iot.com/products/SerialServer/SP501LW>

> **定位。** 本仓库是一台**可直接出厂的串口服务器**：开机即可登录 Web，
> 配置网络、串口、工作模式（透传桥），数据立刻在串口与所选协议之间双向流动。
> 同时保留"接口层 + 业务层"的清晰分界——所有协议策略、设备云对接、
> 行业协议（如 Modbus）仍按"新建业务 component"的方式扩展，**不污染服务层**。

---

## 功能一览

- **登录鉴权**——本地账号 + SHA-256(salt‖密码) 哈希 + 内存 Bearer token；
  出厂默认 `admin / admin`，首次登录强制改密。
- **统一配置**——所有持久化项走 `config_service`，键集中注册在
  `config_keys.h`。任何模块禁止直写 NVS。
- **统一串口**——单一 UART 抽象：帧间隙 RX 聚合、热重配、最多 4 路
  RX 订阅、RS-485 半双工。
- **统一协议**——单 vtable 覆盖 MQTT · TCP 客户端 · TCP 服务器 ·
  UDP · HTTP 客户端，五种均为真实实现，可由工厂函数切换。
- **工作模式（透传桥）**——`bridge_service` 把串口与所选协议双向
  绑定：串口 RX → 协议 send，协议 RX → 串口 send。Web 上一键切换。
- **网络服务**——WiFi AP+STA、以太网（如硬件支持）、静态 IP、
  指数退避重连，以及首次配网用的 captive-portal DNS。
- **精简 Web UI**——五页 SPA：基本信息、配网、串口、工作模式、串口调试
  控制台（WebSocket）。HTML/CSS/原生 ES Module，无任何前端框架。
  自适应手机/电脑，无 emoji，图标全部 SVG。
- **可换板 HAL**——GPIO · UART · Timer 适配器，业务/服务层不直接
  包含芯片驱动头文件。

---

## 架构总览

```
┌──────────────────────────────────────────────┐
│  web_minimal  (REST + WebSocket + 静态 SPA)  │
└──────┬─────────────┬──────────┬──────────────┘
       │ uses        │ uses     │ uses
       ▼             ▼          ▼
┌────────────┐ ┌─────────────┐ ┌──────────────┐
│ auth       │ │ bridge      │ │ net_service  │
│ _service   │ │ _service    │ │ (wifi/eth)   │
└─────┬──────┘ └──┬───────┬──┘ └──────┬───────┘
      │           │       │           │
      │           ▼       ▼           ▼
      │   ┌──────────┐ ┌──────────┐ ┌──────────┐
      │   │ serial   │ │ protocol │ │ hal_     │
      │   │ _service │ │ _service │ │ adapters │
      │   └──────────┘ └──────────┘ └──────────┘
      ▼
  ┌──────────────┐
  │ config_      │  ← 所有持久化的唯一入口
  │ service      │
  └──────────────┘
```

**强制纪律**（PR 评审会按这条审）：

1. 协议模块绝不直接碰 UART 驱动。
2. 串口模块绝不依赖任何协议的连接状态。
3. 上层模块（含 Web）绝不绕过 `config_service` 写 NVS。
4. Web 层只调用服务接口，不直接操作驱动。
5. 业务策略（透传节流、Modbus 帧解析、上报模板…）放业务 component，
   不塞进接口层。

## 目录结构

```
main/                  仅做启动顺序编排（app_main.c）
components/
  config_service/      NVS 键值存储 + 中心键注册表
  serial_service/      UART 门面（配置、TX、多路 RX、状态）
  net_service/         WiFi AP+STA、以太网、重连、扫描、captive DNS
  protocol_service/    协议统一接口 + mqtt/tcp/udp/http 实现
  hal_adapters/        gpio / uart / timer 适配器
  auth_service/        登录账号 + 哈希 + 会话 token
  bridge_service/      串口 ↔ 协议 双向透传桥（工作模式）
  web_minimal/         HTTP 服务、REST API、WebSocket、SPA 资源
  ethernet_init/       ESP-IDF 以太网示例（未修改）
docs/
  API.md               Web REST / WebSocket 契约
  INTERFACES.md        每个服务的 C 接口速查
  DEVELOPMENT_TUTORIAL.md  二次开发实战
  STUBS.md             明确不在本固件内的事项
examples/
  bridge_serial_tcp_demo/  独立的 bridge 教学样例（与 bridge_service 思路一致）
```

---

## 编译与烧录

依赖：[ESP-IDF **v5.5** 或更新](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/index.html)
（本仓库以 v5.5.4 为基线测试）。

```bash
git clone https://github.com/shodan1q/openlk.git
cd openlk

. $IDF_PATH/export.sh          # 或 ~/esp/esp-idf/export.sh
idf.py set-target esp32
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

首次开机：

1. 设备启动 SoftAP `Gateway-XXXXXX`（XXXXXX 为 AP MAC 末三字节）。
2. 手机/电脑连接该 SoftAP，captive-portal DNS 会把任意 HTTP
   请求重定向到 `http://192.168.4.1/`。
3. 默认账号 **admin / admin**，登录后被强制修改密码。

> 由于固件含登录、工作模式、基本信息等多个组件，体积约 1.1 MB。
> `sdkconfig.defaults` 里把分区表切到 `SINGLE_APP_LARGE`（1.5 MB factory，
> 适配 SP501LW 的 2 MB flash）。

---

## Web UI 简介

| 路由         | 用途 |
|--------------|------|
| `#/info`     | 固件 / 芯片 / 网络 / 资源 等基本信息 + 重启入口 |
| `#/network`  | 上行 WiFi/以太网 + AP 热点（SSID / 密码 / 信道） |
| `#/serial`   | 波特率、数据/停止位、校验、流控、RS-485、帧间隙 |
| `#/workmode` | 关闭 / TCP 客户端 / TCP 服务器 / UDP / MQTT / HTTP |
| `#/console`  | 实时串口调试：HEX/TEXT 发送、WebSocket 实时收 |

完整 REST + WebSocket 契约见 [`docs/API.md`](docs/API.md)；所有响应统一信封：

```json
{ "code": 0, "msg": "ok", "data": { } }
```

受保护接口须在请求头携带 `Authorization: Bearer <token>`，
WebSocket 由于浏览器无法注入头，改为 `?token=<token>` 形式传递。

---

## 接口速览

完整速查见 [`docs/INTERFACES.md`](docs/INTERFACES.md)。常用片段：

```c
/* 配置 */
config_service_init();
config_get_str(CFG_KEY_WIFI_SSID, buf, sizeof(buf), "");
config_set_int(CFG_KEY_SER_BAUD, 115200);
config_commit();

/* 串口 */
serial_service_init();
serial_service_register_rx_cb(on_rx, NULL);
serial_service_start();
serial_service_send(data, len);

/* 协议（五种共用同一组操作） */
protocol_handle_t *mq = protocol_mqtt_create();
protocol_register_event_cb(mq, on_state, NULL);
protocol_register_rx_cb   (mq, on_rx,    NULL);
protocol_start(mq);
protocol_send(mq, data, len);

/* 工作模式（透传桥） */
bridge_service_init();
bridge_service_start();                          /* 按当前持久化模式启动 */
bridge_service_apply_mode(BRIDGE_MODE_TCP_CLIENT); /* 切换并落盘 */

/* 鉴权 */
auth_service_init();                /* 出厂 admin/admin */
auth_service_login(user, pass, token, &must_change);

/* 网络 */
net_status_t st;
net_service_get_status(&st);
net_service_scan(aps, MAX_APS, &n);
```

二次开发实战教程：[`docs/DEVELOPMENT_TUTORIAL.md`](docs/DEVELOPMENT_TUTORIAL.md)。

---

## 扩展指南

| 想加什么？ | 怎么做 |
|-----------|--------|
| 新外设 | 在 `components/hal_adapters/` 加 header/impl |
| 新协议 | 复制 `components/protocol_service/src/protocol_stub_common.h`，替换 vtable |
| 业务桥（如串口 → 自定义协议） | 新建 component，依赖 `serial_service` + `protocol_service`；**不要**改接口层 |
| Modbus RTU↔TCP / 平台上报 | 同上，新业务 component |
| 不同板型 | 改 `serial_service.c` 的引脚宏，或 `ethernet_init` 的 Kconfig PHY |

`bridge_service` 提供"通用透传"，已经覆盖 90% 的串口服务器场景。
若你需要协议封装、行业协议解析、心跳/重连策略增强，仍按上面"新业务
component"的方式做。

---

## 贡献

欢迎提 PR。提交前请自检：

- 接口层不夹带业务策略（透传节流、Modbus 解析、上报模板、工作模式编排…），
  这些一律放新业务 component。
- 遵循现有代码风格（C 代码留空格，注释解释 **为什么**，不是 *做了什么*）。
- `idf.py build` 工程源码零警告。

Bug / 需求请到 GitHub Issue 反馈。

---

## License

[Apache License 2.0](LICENSE). © 2026 立控电子.
