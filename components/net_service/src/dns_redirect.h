#pragma once

#include <stdint.h>
#include "esp_err.h"

/*
 * DNS redirector — internal helper for net_service.
 * Private header, keep out of include/.
 *
 * `redirect_ip_be` is the IP returned for every A query, in network byte order
 * (the same shape as `esp_netif_ip_info_t::ip.addr`).
 */
esp_err_t dns_redirect_start(uint32_t redirect_ip_be);
void      dns_redirect_stop(void);
