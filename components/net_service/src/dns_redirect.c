/*
 * Minimal DNS redirector — answers every A-record query with `redirect_ip`.
 *
 * Used only in SoftAP mode so that phones auto-open the configuration page
 * (captive-portal behaviour).  Not a general-purpose DNS server; non-A
 * queries are answered with an empty response.
 */

#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "lwip/sockets.h"
#include "lwip/err.h"

#include "dns_redirect.h"

static const char *TAG = "dns_redir";

#define DNS_PORT       53
#define DNS_MAX_LEN    512

/* RFC 1035 header (network-order fields, 12 bytes). */
typedef struct __attribute__((packed)) {
    uint16_t id;
    uint16_t flags;
    uint16_t qd;
    uint16_t an;
    uint16_t ns;
    uint16_t ar;
} dns_hdr_t;

/* A-record answer appended after the question. `name_ptr` is a compression
 * pointer back to the question at offset 12 (0xC00C). */
typedef struct __attribute__((packed)) {
    uint16_t name_ptr;
    uint16_t type;
    uint16_t class;
    uint32_t ttl;
    uint16_t rdlen;
    uint32_t ip;
} dns_a_answer_t;

static uint32_t s_redirect_ip;
static TaskHandle_t s_task;
static volatile bool s_stop;

/* Returns offset after the QNAME (labels terminated with 0), or -1 on parse error. */
static int skip_qname(const uint8_t *buf, int len, int off)
{
    while (off < len) {
        uint8_t l = buf[off++];
        if (l == 0) return off;
        if ((l & 0xC0) == 0xC0) { if (off >= len) return -1; return off + 1; }
        if (l > 63 || off + l > len) return -1;
        off += l;
    }
    return -1;
}

static void dns_task(void *arg)
{
    (void)arg;
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) { ESP_LOGE(TAG, "socket: %d", errno); vTaskDelete(NULL); return; }

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_addr.s_addr = htonl(INADDR_ANY),
        .sin_port = htons(DNS_PORT),
    };
    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        ESP_LOGE(TAG, "bind :53: %d", errno);
        close(sock);
        vTaskDelete(NULL);
        return;
    }
    struct timeval tv = { .tv_sec = 1 };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    ESP_LOGI(TAG, "captive portal DNS up on :53");

    uint8_t buf[DNS_MAX_LEN];

    while (!s_stop) {
        struct sockaddr_in peer;
        socklen_t plen = sizeof(peer);
        int n = recvfrom(sock, buf, sizeof(buf), 0,
                        (struct sockaddr *)&peer, &plen);
        if (n < (int)sizeof(dns_hdr_t)) continue;

        dns_hdr_t *h = (dns_hdr_t *)buf;
        /* Ignore anything that isn't a standard query. */
        if ((ntohs(h->flags) & 0x8000) != 0) continue;  /* already a response */

        int qend = skip_qname(buf, n, sizeof(dns_hdr_t));
        if (qend < 0 || qend + 4 > n) continue;
        uint16_t qtype  = (buf[qend] << 8) | buf[qend + 1];
        uint16_t qclass = (buf[qend + 2] << 8) | buf[qend + 3];
        int qtail = qend + 4;

        /* Build response in place: set QR=1, RA=1, preserve RD; clear ancount
         * by default. The question stays as-is. */
        h->flags = htons(0x8180);   /* QR | RD | RA, RCODE=0 */
        h->an    = 0;
        h->ns    = 0;
        h->ar    = 0;

        int resp_len = qtail;

        if (qtype == 1 && qclass == 1 && qtail + (int)sizeof(dns_a_answer_t) <= (int)sizeof(buf)) {
            dns_a_answer_t ans = {
                .name_ptr = htons(0xC00C),     /* pointer to QNAME */
                .type     = htons(1),
                .class    = htons(1),
                .ttl      = htonl(60),
                .rdlen    = htons(4),
                .ip       = s_redirect_ip,     /* already network order */
            };
            memcpy(buf + qtail, &ans, sizeof(ans));
            h->an = htons(1);
            resp_len = qtail + sizeof(ans);
        }

        sendto(sock, buf, resp_len, 0, (struct sockaddr *)&peer, plen);
    }

    close(sock);
    ESP_LOGI(TAG, "captive portal DNS stopped");
    vTaskDelete(NULL);
}

esp_err_t dns_redirect_start(uint32_t redirect_ip_be)
{
    if (s_task) return ESP_OK;
    s_redirect_ip = redirect_ip_be;
    s_stop = false;
    BaseType_t ok = xTaskCreate(dns_task, "dns_redir", 3072, NULL, 4, &s_task);
    return ok == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

void dns_redirect_stop(void)
{
    s_stop = true;
    s_task = NULL;   /* task self-deletes once it observes the flag */
}
