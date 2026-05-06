# Secondary Development Tutorial (Interface-First)

本文是一份在 `openlk` 基础上进行二次开发的实战教程，重点回答一个问题：

如何在**不改底层服务层**的前提下，利用现有接口快速实现业务功能。

目标读者：

- 需要在此固件上开发网关业务逻辑的工程师
- 需要把串口数据桥接到 TCP/MQTT/UDP/HTTP 的开发者
- 需要通过 Web API 做设备编排的上位机/平台开发者

---

## 0. 开发原则（先立规矩）

`openlk` 是“基础分发固件”，分层边界是核心资产。请遵守：

1. 业务逻辑放在新 component 中，不塞进 service 层。
2. 所有持久化配置统一走 `config_service`。
3. 串口统一走 `serial_service`，禁止直接碰 UART driver。
4. 网络协议统一走 `protocol_service`，禁止业务层直接写 socket。
5. Web 只调用服务接口，不直接操作驱动。

推荐先读：

1. `README.md`
2. `docs/INTERFACES.md`
3. `docs/API.md`
4. `main/app_main.c`

---

## 1. 能力地图：你能复用什么

常用能力按接口分组如下：

- 配置与持久化：`config_service`
- 串口收发：`serial_service`
- 网络状态/扫描/重启：`net_service`
- 多协议统一抽象：`protocol_service`
- Web 管理面：`web_minimal`（REST + WS）

建议思路：

- 能力层只“提供能力”
- 你的 component 只“组合能力”

---

## 2. 目标案例：串口 ↔ TCP Client 双向透传

我们用一个完整可落地案例贯穿全文：

- 串口收到数据 -> 发给 TCP 远端
- TCP 收到数据 -> 写回串口
- TCP 断线重连由 `protocol_tcp_client` 内部处理
- bridge 只做数据转发与简单节流/观测

为什么是这个案例：

- 涵盖最核心接口组合：`serial_service + protocol_service`
- 真实场景覆盖高
- 后续切换到 MQTT/UDP 只需要替换工厂函数和编码逻辑

---

## 3. 第一步：新建业务组件

目录结构：

```text
components/
  bridge_serial_tcp/
    CMakeLists.txt
    include/bridge_serial_tcp.h
    src/bridge_serial_tcp.c
```

`components/bridge_serial_tcp/CMakeLists.txt`：

```cmake
idf_component_register(
    SRCS         "src/bridge_serial_tcp.c"
    INCLUDE_DIRS "include"
    REQUIRES     serial_service protocol_service
)
```

`components/bridge_serial_tcp/include/bridge_serial_tcp.h`：

```c
#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t bridge_serial_tcp_start(void);
esp_err_t bridge_serial_tcp_stop(void);

#ifdef __cplusplus
}
#endif
```

---

## 4. 第二步：实现 bridge（生产可用写法）

下面这份示例代码重点解决了几个常见 bug：

- `stop()` 先删队列导致任务并发访问（竞态）
- 在组件函数里用 `ESP_ERROR_CHECK` 导致不可恢复错误直接崩溃
- 回调阻塞过长导致串口任务抖动

`components/bridge_serial_tcp/src/bridge_serial_tcp.c`：

```c
#include "bridge_serial_tcp.h"

#include <stdbool.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "esp_log.h"

#include "serial_service.h"
#include "protocol_service.h"

typedef struct {
    size_t  len;
    uint8_t buf[512];
} frame_t;

static const char *TAG = "bridge_s2t";

static QueueHandle_t      s_q;
static TaskHandle_t       s_task;
static protocol_handle_t *s_tcp;
static volatile bool      s_running;
static uint32_t           s_drop_cnt;

static void on_serial_rx(const uint8_t *data, size_t len, void *user)
{
    (void)user;
    if (!s_running || !s_q || !data || len == 0) return;

    frame_t f = {0};
    f.len = len > sizeof(f.buf) ? sizeof(f.buf) : len;
    memcpy(f.buf, data, f.len);

    /* 回调在串口任务上下文，不阻塞。 */
    if (xQueueSend(s_q, &f, 0) != pdTRUE) {
        s_drop_cnt++;
    }
}

static void on_tcp_rx(protocol_handle_t *h, const uint8_t *data, size_t len, void *user)
{
    (void)h;
    (void)user;
    if (!s_running || !data || len == 0) return;

    /* 这里是协议线程上下文，保持轻量。 */
    serial_service_send(data, len);
}

static void on_tcp_state(protocol_handle_t *h, protocol_state_t st, void *user)
{
    (void)h;
    (void)user;
    ESP_LOGI(TAG, "tcp state=%d", (int)st);
}

static void tx_task(void *arg)
{
    (void)arg;

    frame_t f;
    for (;;) {
        if (xQueueReceive(s_q, &f, pdMS_TO_TICKS(200)) == pdTRUE) {
            if (s_running) {
                esp_err_t err = protocol_send(s_tcp, f.buf, f.len);
                if (err != ESP_OK) {
                    ESP_LOGW(TAG, "protocol_send: %s", esp_err_to_name(err));
                }
            }
            continue;
        }
        if (!s_running) break;
    }

    if (s_drop_cnt) {
        ESP_LOGW(TAG, "dropped frames=%lu", (unsigned long)s_drop_cnt);
        s_drop_cnt = 0;
    }

    s_task = NULL;
    vTaskDelete(NULL);
}

static esp_err_t start_worker(void)
{
    BaseType_t ok = xTaskCreate(tx_task, "bridge_tx", 4096, NULL, 8, &s_task);
    if (ok != pdPASS) {
        s_task = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

static void stop_worker(void)
{
    if (!s_task) return;

    /* 等待任务自退，避免删除 queue 时并发访问。 */
    for (int i = 0; i < 50 && s_task; ++i) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    if (s_task) {
        ESP_LOGW(TAG, "tx task stop timeout, force delete");
        vTaskDelete(s_task);
        s_task = NULL;
    }
}

esp_err_t bridge_serial_tcp_start(void)
{
    if (s_running) return ESP_OK;

    s_tcp = protocol_tcp_client_create();
    if (!s_tcp) return ESP_ERR_NO_MEM;

    s_q = xQueueCreate(16, sizeof(frame_t));
    if (!s_q) {
        protocol_destroy(s_tcp);
        s_tcp = NULL;
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = protocol_register_rx_cb(s_tcp, on_tcp_rx, NULL);
    if (err != ESP_OK) goto fail;

    err = protocol_register_event_cb(s_tcp, on_tcp_state, NULL);
    if (err != ESP_OK) goto fail;

    err = serial_service_register_rx_cb(on_serial_rx, NULL);
    if (err != ESP_OK) goto fail;

    err = protocol_start(s_tcp);
    if (err != ESP_OK) goto fail_unreg_serial;

    s_running = true;
    err = start_worker();
    if (err != ESP_OK) goto fail_stop_proto;

    ESP_LOGI(TAG, "started");
    return ESP_OK;

fail_stop_proto:
    s_running = false;
    protocol_stop(s_tcp);
fail_unreg_serial:
    serial_service_unregister_rx_cb(on_serial_rx);
fail:
    protocol_destroy(s_tcp);
    s_tcp = NULL;
    if (s_q) {
        vQueueDelete(s_q);
        s_q = NULL;
    }
    return err;
}

esp_err_t bridge_serial_tcp_stop(void)
{
    if (!s_running) return ESP_OK;

    s_running = false;
    serial_service_unregister_rx_cb(on_serial_rx);
    stop_worker();

    protocol_stop(s_tcp);
    protocol_destroy(s_tcp);
    s_tcp = NULL;

    if (s_q) {
        vQueueDelete(s_q);
        s_q = NULL;
    }

    ESP_LOGI(TAG, "stopped");
    return ESP_OK;
}
```

### 4.1 设计说明

- 回调只做轻操作（入队/转发），避免阻塞底层任务。
- 有限队列做削峰，丢包可观测（`s_drop_cnt`）。
- 生命周期明确：`start` 创建资源，`stop` 先停 worker，再回收资源。
- 错误处理可恢复：返回 `esp_err_t`，由上层决定策略。

---

## 5. 第三步：接入启动流程

在 `main/CMakeLists.txt` 添加依赖：

```cmake
REQUIRES ... bridge_serial_tcp
```

在 `main/app_main.c` 添加：

```c
#include "bridge_serial_tcp.h"
...
ESP_ERROR_CHECK(bridge_serial_tcp_start());
```

推荐顺序：

1. `config_service_init()`
2. `net_service_init()`
3. `serial_service_init()`
4. `serial_service_start()`
5. `bridge_serial_tcp_start()`
6. `web_minimal_start()`

---

## 6. 第四步：配置约定（不重复造轮子）

本案例直接复用已存在配置键：

- `CFG_KEY_TCP_HOST`
- `CFG_KEY_TCP_PORT`
- `CFG_KEY_TCP_RECONN_MS`

调试期可在代码中写默认值：

```c
config_set_str(CFG_KEY_TCP_HOST, "192.168.1.10");
config_set_int(CFG_KEY_TCP_PORT, 9000);
config_set_int(CFG_KEY_TCP_RECONN_MS, 2000);
config_commit();
```

量产建议：

- 用 Web API 扩展配置页面写入这些键。
- 不在业务代码里硬编码环境参数。

---

## 7. 验证闭环（从功能到异常）

### 7.1 基础链路

PC 启 TCP 监听：

```bash
nc -l 9000
```

烧录并查看日志：

```bash
idf.py -p /dev/ttyUSB0 flash monitor
```

期望：

- 串口 -> TCP：PC 能看到数据
- TCP -> 串口：串口设备能收到数据

### 7.2 异常场景

1. 断开 WiFi/上游网络：观察 `protocol_state` 变化
2. 恢复网络：验证自动重连后恢复转发
3. 高频突发串口：观察 `dropped frames` 是否增长

---

## 8. 常见 bug 清单（项目里最容易踩）

1. `stop()` 阶段资源回收顺序错误
- 先删队列再停任务，容易竞态。

2. 回调里做慢操作
- 串口/协议回调里做阻塞 I/O，会放大抖动。

3. 业务层直接访问底层 driver
- 破坏分层，后续维护成本暴涨。

4. 所有错误码都返回 `ESP_FAIL`
- 上层无法区分参数、状态、资源错误。

5. 忽略可观测性
- 没有 drop/last_err/state 统计，现场定位困难。

---

## 9. 如何“只用接口”切换到其他业务

### 9.1 串口 -> MQTT

- `protocol_tcp_client_create()` 换成 `protocol_mqtt_create()`
- 加一个编码函数，把串口帧编码成 JSON/二进制
- 其余生命周期逻辑基本不变

### 9.2 串口 -> UDP

- 工厂换 `protocol_udp_create()`
- 队列和 worker 逻辑可复用

### 9.3 Modbus 网关

- 在 worker 中加状态机（请求匹配、超时、重发）
- service 层仍保持不改

---

## 10. 重点：只用现有 Web 接口也能实现功能

很多功能不需要立刻改固件代码，可以先用 Web API 编排：

### 10.1 功能 A：自动配网并重启生效

```bash
curl -s http://192.168.4.1/api/network

curl -s -X POST http://192.168.4.1/api/network \
  -H 'Content-Type: application/json' \
  -d '{
    "mode":"wifi",
    "dhcp":true,
    "ssid":"office-ap",
    "password":"12345678"
  }'

curl -s -X POST http://192.168.4.1/api/system/reboot -H 'Content-Type: application/json' -d '{}'
```

### 10.2 功能 B：远程串口调试脚本

```bash
# 读当前串口配置
curl -s http://192.168.4.1/api/serial

# 更新串口参数
curl -s -X POST http://192.168.4.1/api/serial \
  -H 'Content-Type: application/json' \
  -d '{"baud":9600,"data_bits":8,"stop_bits":1,"parity":"none","flow_ctrl":false,"rs485":false,"frame_gap_ms":10}'

# 发 HEX 指令
curl -s -X POST http://192.168.4.1/api/serial/send \
  -H 'Content-Type: application/json' \
  -d '{"fmt":"hex","data":"01 03 00 00 00 02 C4 0B"}'
```

### 10.3 功能 C：扫描网络并筛选最优 AP

```bash
curl -s http://192.168.4.1/api/network/scan
```

你可以在上位机按 `rssi`、`auth` 做策略筛选，再调用 `POST /api/network` 下发。

### 10.4 功能 D：前端实时串口监视

- REST 用于发送：`POST /api/serial/send`
- WebSocket 用于接收：`/ws/serial`

浏览器侧最小示例：

```js
const ws = new WebSocket(`ws://${location.host}/ws/serial`);
ws.onmessage = (e) => {
  const f = JSON.parse(e.data);
  if (f.dir === "rx") console.log(f.ts, f.data);
};
```

---

## 11. 代码评审清单（提交前）

提交业务 component 前请自检：

1. 是否只使用公开头文件（无 private include）
2. 回调是否无阻塞、无大内存分配
3. 是否具备优雅停止路径（任务退出后再回收资源）
4. 错误码是否可区分（参数/状态/资源）
5. 是否有最小可观测指标（drop/last_err/state）
6. `idf.py build` 是否通过

---

## 12. 下一步建议

1. 增加 `bridge_serial_mqtt` 案例（含 topic 规划与 QoS）
2. 增加“二进制协议编解码层”模板（CRC、重发窗口）
3. 增加“业务健康检查 API”（bridge 统计信息对外暴露）

如果你希望，我可以继续在仓库里直接补第二篇教程：

- `docs/DEVELOPMENT_MQTT_TUTORIAL.md`
