# bridge_serial_tcp_demo

独立 ESP-IDF 示例工程（位于 openlk 仓库内部），演示：

- 串口 RX -> TCP Client 发送
- TCP Client RX -> 串口发送
- 使用 openlk 已有服务接口组合业务，不改 service 层

## 目录说明

- `main/app_main.c`：示例启动流程
- `components/bridge_serial_tcp`：业务 bridge 组件
- `components/*`：示例工程内置完整依赖组件，可独立编译，不依赖上级仓库 `components/`

## 构建

```bash
cd examples/bridge_serial_tcp_demo
. $IDF_PATH/export.sh
idf.py set-target esp32
idf.py build
```

## 烧录

```bash
idf.py -p /dev/ttyUSB0 flash monitor
```

## 默认行为

- 若 `tcp.host/tcp.port/tcp.reconn` 未配置，`app_main` 会写入默认值：
  - `tcp.host=192.168.1.100`
  - `tcp.port=9000`
  - `tcp.reconn=2000`
- Web 管理界面仍可用（端口 80），可用于配置配网/桥接/串口并查看调试信息。

## 调试建议

PC 侧可先开 TCP 监听：

```bash
nc -l 9000
```

然后观察双向数据是否打通。
