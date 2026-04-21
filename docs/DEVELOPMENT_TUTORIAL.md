# Secondary Development Tutorial (Interface-First)

本文是一份“在 `openlk` 基础上做二次开发”的实战教程，重点讲：

1. 不改底层服务层（`config/serial/net/protocol/web`）；
2. 只通过公开接口实现业务功能；
3. 用一个完整案例串起来：**串口 ↔ TCP Client 双向透传 bridge**。

---

## 0. 先建立正确心智

`openlk` 的定位是“业务底座”，不是最终业务固件。
开发时请遵守这一条：

- 接口层（services）负责能力，不负责业务策略。
- 你的业务写在**新 component**里（例如 `bridge_serial_tcp`）。

推荐阅读顺序：

1. `README.md`（分层与边界）
2. `docs/INTERFACES.md`（函数入口速查）
3. `docs/API.md`（Web 接口契约）
4. `main/app_main.c`（启动顺序）

---

## 1. 案例目标：串口 ↔ TCP Client 双向透传

功能定义：

- 串口收到数据，转发给 TCP 客户端连接的远端；
- TCP 收到数据，写回串口；
- 断线自动重连由 `protocol_tcp_client` 负责；
- bridge 只负责“转发”，不关心 WiFi 细节和 socket 细节。

为什么选这个案例：

- 覆盖 `serial_service` + `protocol_service` 两个核心接口；
- 最接近真实网关需求；
- 后续切 MQTT/UDP/HTTP 时复用框架几乎不变。

---

## 2. 第一步：创建业务组件骨架

在 `components/` 下新增目录：

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
    SRCS        "src/bridge_serial_tcp.c"
    INCLUDE_DIRS "include"
    REQUIRES    serial_service protocol_service
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

## 3. 第二步：实现 bridge 逻辑（只用公开接口）

`components/bridge_serial_tcp/src/bridge_serial_tcp.c`：

```c
#include "bridge_serial_tcp.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "serial_service.h"
#include "protocol_service.h"

static const char *TAG = "bridge_s2t";

typedef struct {
    size_t  len;
    uint8_t buf[512];
} frame_t;

static QueueHandle_t       s_q;
static TaskHandle_t        s_task;
static protocol_handle_t  *s_tcp;
static bool                s_running;

static void on_serial_rx(const uint8_t *data, size_t len, void *user)
{
    (void)user;
    if (!s_running || !s_q || !data || len == 0) return;

    frame_t f = {0};
    f.len = len > sizeof(f.buf) ? sizeof(f.buf) : len;
    memcpy(f.buf, data, f.len);
    /* 回调在串口任务上下文，不能阻塞太久。 */
    xQueueSend(s_q, &f, 0);
}

static void on_tcp_rx(protocol_handle_t *h, const uint8_t *data, size_t len, void *user)
{
    (void)h; (void)user;
    if (!s_running || !data || len == 0) return;
    /* 直接写串口，策略层不关心 socket 细节。 */
    serial_service_send(data, len);
}

static void on_tcp_state(protocol_handle_t *h, protocol_state_t st, void *user)
{
    (void)h; (void)user;
    ESP_LOGI(TAG, "tcp state=%d", (int)st);
}

static void tx_task(void *arg)
{
    (void)arg;
    frame_t f;
    while (s_running) {
        if (xQueueReceive(s_q, &f, pdMS_TO_TICKS(200)) == pdTRUE) {
            esp_err_t err = protocol_send(s_tcp, f.buf, f.len);
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "protocol_send: %s", esp_err_to_name(err));
            }
        }
    }
    vTaskDelete(NULL);
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

    ESP_ERROR_CHECK(protocol_register_rx_cb(s_tcp, on_tcp_rx, NULL));
    ESP_ERROR_CHECK(protocol_register_event_cb(s_tcp, on_tcp_state, NULL));
    ESP_ERROR_CHECK(serial_service_register_rx_cb(on_serial_rx, NULL));
    ESP_ERROR_CHECK(protocol_start(s_tcp));

    s_running = true;
    BaseType_t ok = xTaskCreate(tx_task, "bridge_tx", 4096, NULL, 8, &s_task);
    if (ok != pdPASS) {
        s_running = false;
        serial_service_unregister_rx_cb(on_serial_rx);
        protocol_stop(s_tcp);
        protocol_destroy(s_tcp);
        s_tcp = NULL;
        vQueueDelete(s_q);
        s_q = NULL;
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "started");
    return ESP_OK;
}

esp_err_t bridge_serial_tcp_stop(void)
{
    if (!s_running) return ESP_OK;
    s_running = false;

    serial_service_unregister_rx_cb(on_serial_rx);
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

实现要点：

- `serial_rx_cb` 只做轻操作（拷贝入队），不做网络阻塞操作；
- 真正发送放到独立任务中，避免拖慢串口接收线程；
- 所有网络连接/重连由 `protocol_tcp_client` 内部实现，bridge 不重复造轮子。

---

## 4. 第三步：接入启动流程

在 `main/CMakeLists.txt` 的 `REQUIRES` 增加 `bridge_serial_tcp`。

在 `main/app_main.c`：

```c
#include "bridge_serial_tcp.h"
...
ESP_ERROR_CHECK(bridge_serial_tcp_start());
```

建议顺序：

1. `config_service_init()`
2. `net_service_init()`
3. `serial_service_init()/start()`
4. `bridge_serial_tcp_start()`
5. `web_minimal_start()`

这样 Web 页面可用于诊断 bridge 状态（串口参数、网络状态、串口控制台）。

---

## 5. 第四步：配置项约定（接口化）

本案例 TCP 参数直接复用已有配置键（见 `config_keys.h`）：

- `CFG_KEY_TCP_HOST`
- `CFG_KEY_TCP_PORT`
- `CFG_KEY_TCP_RECONN_MS`

你可以用两种方式写入：

1. 通过已有 Web API 扩展页面（推荐）；
2. 固件启动时写默认值（仅调试）。

示例（调试阶段）：

```c
config_set_str(CFG_KEY_TCP_HOST, "192.168.1.10");
config_set_int(CFG_KEY_TCP_PORT, 9000);
config_set_int(CFG_KEY_TCP_RECONN_MS, 2000);
config_commit();
```

---

## 6. 验证步骤（最小闭环）

### 6.1 准备 TCP 服务端

PC 上启动监听：

```bash
nc -l 9000
```

### 6.2 烧录并配网

```bash
idf.py -p /dev/ttyUSB0 flash monitor
```

用 Web 页面配置好：

- 设备联网（STA got IP）
- 串口参数正确
- TCP host/port 指向你的 PC

### 6.3 双向验证

1. 串口设备发数据 -> PC `nc` 能看到；
2. 在 `nc` 输入数据 -> 串口设备能收到；
3. 拔掉网线或断 WiFi -> 看 `tcp state` 变化，恢复后自动重连。

---

## 7. 常见坑与规避

1. 串口回调里做阻塞发送  
现象：丢帧、WDT、吞吐下降。  
做法：回调只入队，发送放任务。

2. 在 bridge 里直接用 `driver/uart.h` 或 socket API  
现象：耦合底层、后续换协议困难。  
做法：只调 `serial_service` + `protocol_service`。

3. 到处直接写 NVS 字符串 key  
现象：键名分散、迁移困难。  
做法：只经 `config_service`，并使用 `CFG_KEY_*`。

4. 忽略协议状态  
现象：断线时无脑发送，日志刷屏。  
做法：订阅 `protocol_event_cb`，必要时做限流/降级。

---

## 8. 如何从本案例扩展到其他功能

### 8.1 串口 -> MQTT 上报

- 把 `protocol_tcp_client_create()` 替换成 `protocol_mqtt_create()`；
- 串口回调里做业务编帧（JSON/二进制）；
- `protocol_send()` 发布到配置的 topic。

### 8.2 UDP 透传

- 工厂换为 `protocol_udp_create()`；
- 其余 bridge 主体可复用。

### 8.3 Modbus 网关

- bridge 中新增“协议解析层”（请求匹配、超时、重发）；
- 仍不改 service 层，只在业务 component 做状态机。

---

## 9. 建议的代码评审清单

提交业务 component 前，逐条自查：

1. 是否仅使用公开头文件（不 include 私有内部头）；
2. 回调是否短小且无阻塞；
3. 错误码是否向上返回，不吞错；
4. 配置键是否全部来自 `config_keys.h`；
5. `idf.py build` 是否通过。

---

如果你需要，我可以在下一版教程里补一套“`bridge_serial_mqtt` 完整代码骨架”，并附上 Web 配置页面字段设计建议。
