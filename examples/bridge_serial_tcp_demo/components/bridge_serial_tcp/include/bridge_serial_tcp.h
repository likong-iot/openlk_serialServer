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
