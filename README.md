# openlk — SP501LW Base Distribution Firmware

Open-source base firmware (SDK skeleton) for the **Likong SP501LW** serial
gateway and pin-compatible ESP32-based serial-to-network devices.

Hardware product page: <https://docv2.likong-iot.com/products/SerialServer/SP501LW>

> **Positioning.** This repository is **not a complete business firmware.**
> It is a clean, reusable source-distribution base intended for secondary
> development: every subsystem is a stable, documented interface with a
> minimal implementation behind it. Add your protocol strategy, field
> bus adapters, or device-cloud integration on top without touching the
> interface layer.

---

## Features

- **Unified configuration service** — all persistent settings flow through
  `config_service` and a single central key registry (`config_keys.h`).
  No module writes NVS directly.
- **Unified serial service** — one façade over the UART peripheral, with
  frame-gap RX aggregation, hot reconfiguration, multi-subscriber RX
  callbacks, and RS-485 half-duplex support.
- **Unified protocol abstraction** — a single vtable covers
  MQTT · TCP client · TCP server · UDP · HTTP client. All five are
  real implementations, pluggable via factory functions.
- **Network service** — WiFi AP + STA, Ethernet (where the hardware has it),
  static-IP application, exponential-backoff reconnect, and a built-in
  captive-portal DNS for first-boot configuration.
- **Minimal web UI** — three and only three pages: network config,
  serial config, and a realtime serial debug console (WebSocket). Clean
  ES-module JS, semantic HTML, CSS-variable theming (dark mode included).
- **Board-swappable HAL** — GPIO · UART · Timer adapters so upper layers
  never include chip-specific driver headers.

---

## Architecture

```
┌──────────────────────────────────────────┐
│  web_minimal  (REST + WebSocket, 3 pages)│
└──────────────────┬───────────────────────┘
                   │ uses
       ┌───────────┼────────────┐
       ▼           ▼            ▼
┌────────────┐ ┌──────────┐ ┌─────────────┐
│ config     │ │ serial   │ │ net_service │
│ _service   │ │ _service │ │ (wifi/eth)  │
└─────┬──────┘ └────┬─────┘ └──────┬──────┘
      │            │                │
      │            ▼                ▼
      │      ┌──────────┐    ┌──────────────┐
      │      │  hal_    │    │ protocol_    │
      │      │  adapters│    │ service      │
      │      │ (gpio/   │    │ (mqtt/tcp/   │
      │      │  uart/   │    │  udp/http)   │
      │      │  timer)  │    └──────────────┘
      │      └──────────┘
      ▼
  NVS flash
```

Layering rules enforced across the codebase:

1. Protocol modules must never touch the UART driver directly.
2. The serial module must never depend on a protocol's connection state.
3. No upper layer writes NVS except through `config_service`.
4. The web layer talks only to services, never to drivers.

Folder layout:

```
main/                  Bring-up orchestration only (app_main.c)
components/
  config_service/      NVS-backed key/value store + central key registry
  serial_service/      UART façade (config, TX, RX callbacks, status)
  net_service/         WiFi AP+STA, Ethernet, reconnect, scan, captive DNS
  protocol_service/    Unified protocol interface + mqtt/tcp/udp/http impls
  hal_adapters/        gpio / uart / timer adapters
  web_minimal/         HTTP server, REST APIs, WebSocket, static SPA assets
  ethernet_init/       ESP-IDF sample Ethernet bring-up (unmodified)
docs/
  API.md               Web REST / WebSocket contract
  INTERFACES.md        C interface cheat-sheet for every service
  DEVELOPMENT_TUTORIAL.md
                       Interface-first secondary-development tutorial
  STUBS.md             Intentionally out-of-scope items
```

---

## Build

Prerequisites: [ESP-IDF **v5.5** or newer](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/index.html)
(the project is built and tested against v5.5.4).

```bash
git clone https://github.com/shodan1q/openlk.git
cd openlk

. $IDF_PATH/export.sh          # or ~/esp/esp-idf/export.sh
idf.py set-target esp32
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

On a clean flash the device boots with no WiFi STA credentials and opens
a SoftAP named `Gateway-XXXXXX` (last three bytes of the AP MAC). Connect
with a phone or laptop — the captive-portal DNS will redirect any HTTP
request to the configuration UI at `http://192.168.4.1/`.

Common `sdkconfig` overrides are committed as `sdkconfig.defaults`; local
changes go in `sdkconfig` (git-ignored).

---

## Web UI

Three pages, all served from one SPA:

| Route | Purpose |
|-------|---------|
| `#/network` | WiFi SSID/password, DHCP vs static IP, scan-and-pick AP |
| `#/serial`  | Baud, data/stop/parity, flow control, RS-485 toggle, frame gap |
| `#/console` | Realtime serial debug — HEX/TEXT send, live RX display |

See [`docs/API.md`](docs/API.md) for the full REST + WebSocket contract.
Every endpoint returns the envelope `{ "code": int, "msg": string, "data": object|null }`.

---

## Interface summary

Full cheat-sheet in [`docs/INTERFACES.md`](docs/INTERFACES.md). Highlights:

```c
// config
config_service_init();
config_get_str(CFG_KEY_WIFI_SSID, buf, sizeof(buf), "");
config_set_int(CFG_KEY_SER_BAUD, 115200);
config_commit();

// serial
serial_service_init();
serial_service_register_rx_cb(on_rx, NULL);
serial_service_start();
serial_service_send(data, len);

// protocols (all share the same surface)
protocol_handle_t *mq = protocol_mqtt_create();
protocol_register_event_cb(mq, on_state, NULL);
protocol_register_rx_cb   (mq, on_rx,    NULL);
protocol_start(mq);
protocol_send(mq, data, len);

// network
net_status_t st;
net_service_get_status(&st);
net_service_scan(aps, MAX_APS, &n);
```

Secondary-development tutorial:
[`docs/DEVELOPMENT_TUTORIAL.md`](docs/DEVELOPMENT_TUTORIAL.md)

---

## Extending

| Adding what? | How |
|--------------|-----|
| New peripheral | Drop a header/impl into `components/hal_adapters/` |
| New protocol | Copy `components/protocol_service/src/protocol_stub_common.h` and swap the vtable |
| Business bridge (e.g. serial → MQTT) | Create a **new** component that depends on `serial_service` + `protocol_service` — do **not** modify the interface layer |
| Different board | Override the pin macros in `serial_service.c` or adjust `ethernet_init` Kconfig |

---

## Contributing

PRs are welcome. Before submitting:

- Keep the interface layer free of business policy (transparent-passthrough
  logic, Modbus handling, reporting strategy, work modes, …). Those belong
  in a new bridge or application component, not inside the services.
- Match existing code style (C code is spaced, comments explain **why**).
- Run `idf.py build` with zero warnings from project sources.

Bug reports and feature requests: open a GitHub Issue.

---

## License

[Apache License 2.0](LICENSE). © 2026 Likong IoT.
