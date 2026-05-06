#pragma once

/*
 * Central registry of configuration keys.
 *
 * All modules must reference keys via these macros — never inline literal
 * strings — so the set of persisted settings is auditable from a single file.
 *
 * NVS key length limit is 15 chars including trailing NUL.
 */

/* Network */
#define CFG_KEY_NET_MODE         "net.mode"       /* "wifi" | "eth" */
#define CFG_KEY_NET_DHCP         "net.dhcp"       /* bool */
#define CFG_KEY_NET_IP           "net.ip"         /* str: a.b.c.d */
#define CFG_KEY_NET_MASK         "net.mask"
#define CFG_KEY_NET_GW           "net.gw"
#define CFG_KEY_NET_DNS1         "net.dns1"
#define CFG_KEY_NET_DNS2         "net.dns2"

/* WiFi STA */
#define CFG_KEY_WIFI_SSID        "wifi.ssid"
#define CFG_KEY_WIFI_PASS        "wifi.pass"

/* WiFi AP (fallback) */
#define CFG_KEY_WIFI_AP_SSID     "wifi.apssid"
#define CFG_KEY_WIFI_AP_PASS     "wifi.appass"
#define CFG_KEY_WIFI_AP_CHAN     "wifi.apchan"     /* 1..13 */

/* Auth (web login).
 * Why hashes only: NVS readout via fault-injection should not leak the
 * cleartext password. Why mustchg: factory image ships admin/admin and we
 * must force a change on first successful login. */
#define CFG_KEY_AUTH_USER        "auth.user"        /* str */
#define CFG_KEY_AUTH_PWHASH      "auth.pwhash"      /* hex SHA-256 of salt|pass */
#define CFG_KEY_AUTH_SALT        "auth.salt"        /* hex 16-byte salt */
#define CFG_KEY_AUTH_MUSTCHG     "auth.mustchg"     /* bool */

/* Serial (UART) */
#define CFG_KEY_SER_BAUD         "ser.baud"
#define CFG_KEY_SER_DATA_BITS    "ser.dbit"
#define CFG_KEY_SER_STOP_BITS    "ser.sbit"
#define CFG_KEY_SER_PARITY       "ser.parity"
#define CFG_KEY_SER_FLOW_CTRL    "ser.flow"
#define CFG_KEY_SER_FRAME_GAP    "ser.fgap"
#define CFG_KEY_SER_RS485        "ser.rs485"

/* Work mode (bridge_service).
 * Values: "off" | "tcp_client" | "tcp_server" | "udp" | "mqtt" | "http". */
#define CFG_KEY_WORKMODE         "wm.mode"

/* TCP client (protocol) */
#define CFG_KEY_TCP_HOST         "tcp.host"
#define CFG_KEY_TCP_PORT         "tcp.port"
#define CFG_KEY_TCP_RECONN_MS    "tcp.reconn"

/* TCP server (protocol) */
#define CFG_KEY_TCPS_PORT        "tcps.port"
#define CFG_KEY_TCPS_MAX_CLIENTS "tcps.max"

/* UDP (protocol) */
#define CFG_KEY_UDP_LOCAL_PORT   "udp.local"
#define CFG_KEY_UDP_REMOTE_HOST  "udp.host"
#define CFG_KEY_UDP_REMOTE_PORT  "udp.rport"

/* HTTP client (protocol) */
#define CFG_KEY_HTTP_URL         "http.url"
#define CFG_KEY_HTTP_METHOD      "http.method"     /* "GET" | "POST" */
#define CFG_KEY_HTTP_TIMEOUT_MS  "http.timeout"

/* MQTT (protocol) */
#define CFG_KEY_MQTT_URI         "mqtt.uri"         /* e.g. "mqtt://host:1883" */
#define CFG_KEY_MQTT_CLIENT_ID   "mqtt.client"
#define CFG_KEY_MQTT_USER        "mqtt.user"
#define CFG_KEY_MQTT_PASS        "mqtt.pass"
#define CFG_KEY_MQTT_PUB_TOPIC   "mqtt.pub"
#define CFG_KEY_MQTT_SUB_TOPIC   "mqtt.sub"
#define CFG_KEY_MQTT_QOS         "mqtt.qos"

/* Namespace used by config_service inside NVS. */
#define CONFIG_SERVICE_NVS_NAMESPACE "cfg"
