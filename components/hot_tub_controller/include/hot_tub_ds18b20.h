#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t hot_tub_ds18b20_init(void);
esp_err_t hot_tub_ds18b20_read_temperature(float *temperature);
void water_temperature_monitoring_task(void *arg);


#ifdef __cplusplus
}
#endif
