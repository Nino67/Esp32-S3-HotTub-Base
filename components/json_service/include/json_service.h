#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "cJSON.h"


bool crc32_json_wrapper(const cJSON *json_obj,
                        char *output,
                        size_t output_size,
                        size_t *output_len);




/**
 * @brief  Validate a received JSON string with CRC32 envelope and reconstruct the original JSON if valid
 *
 * @param input The input JSON string with CRC32 envelope
 * @param output Buffer to write the reconstructed original JSON string (without CRC32 envelope)
 * @param output_size Size of the output buffer
 * @param expected_crc Pointer to uint32_t to receive the expected CRC32 value from the input
 * @param computed_crc Pointer to uint32_t to receive the computed CRC32 value from the input
 *
 * @return true if the input is valid and the original JSON was successfully reconstructed, false otherwise
 */



esp_err_t json_service_parse_json(const char *json_str, cJSON **out_json);




esp_err_t json_service_validate_crc32(const char *input,
                                      char *output,
                                      size_t output_size,
                                      uint32_t *expected_crc,
                                      uint32_t *computed_crc);
