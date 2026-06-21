#pragma once

#include "esp_err.h"
// #include "cJSON.h"

bool crc32_json_wrapper(const cJSON *json_obj,
                        char *output,
                        size_t output_size,
                        size_t *output_len);