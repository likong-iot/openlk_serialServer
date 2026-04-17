# Service Interfaces Cheat-sheet

一张表看完四层服务的入口。详情见对应头文件。

## config_service

`components/config_service/include/config_service.h`

| 函数 | 作用 |
|------|------|
| `config_service_init / _deinit`                     | 初始化 NVS |
| `config_get_str/int/bool/blob(key, out, def)`       | 读取，缺失时填默认 |
| `config_set_str/int/bool/blob(key, v)`              | 暂存 |
| `config_commit()`                                   | 落盘 |
| `config_erase(key)`                                 | 删除单键 |
| `config_reset_defaults()`                           | 清空命名空间 |
| `config_validate_int_range(v,min,max)`              | 简单校验 |

键集中定义于 `config_keys.h`，禁止任何调用方内联字符串字面量。

## serial_service

`components/serial_service/include/serial_service.h`

| 函数 | 作用 |
|------|------|
| `serial_service_init / _deinit`          | 创建内部状态（不启动 UART） |
| `serial_service_start / _stop`           | 打开/关闭 UART + rx 任务 |
| `serial_service_configure(cfg)`          | 持久化并热重启生效 |
| `serial_service_get_config(out)`         | 取当前参数 |
| `serial_service_send(data, len)`         | 同步发送 |
| `serial_service_register_rx_cb(cb, arg)` | 多路注册接收回调 (最多 4) |
| `serial_service_unregister_rx_cb(cb)`    | 卸载回调 |
| `serial_service_get_status(out)`         | 统计与错误码 |

回调在内部任务上下文调用——必须短时执行。

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

当前所有协议均为**桩实现**（FR-PRO-005），返回 ESP_OK 并记录日志但不做实际 IO。
加入真实实现的方式：拷贝 `protocol_stub_common.h`，替换 vtable 即可。

## net_service

`components/net_service/include/net_service.h`

| 函数 | 作用 |
|------|------|
| `net_service_init()`              | 启动 TCP/IP、WiFi AP+STA，应用静态 IP、事件驱动重连 |
| `net_service_get_status(out)`     | 当前 SSID / RSSI / IP / uptime / heap |
| `net_service_scan(buf, cap, *n)`  | 同步扫描 AP，去重 SSID |
| `net_service_request_reboot(ms)`  | 延迟软复位 |

## hal_adapters

- `hal_gpio.h` — 配置引脚 / 读写 / 中断注册
- `hal_uart.h` — driver_install / 读写 / 查询可读字节（供 serial_service 使用）
- `hal_timer.h` — esp_timer 薄封装，周期/单次定时器

## web_minimal

`components/web_minimal/include/web_minimal.h`

```c
web_minimal_config_t c = { .port = 80, .max_sockets = 7 };
web_minimal_start(&c);
```

启动后自动注册：
- `/`、`/app.css`、`/app.js`、`/modules/*.js`、`/pages/*.js`（静态）
- `GET/POST /api/network`
- `GET/POST /api/serial`、`POST /api/serial/send`
- `WS /ws/serial`
