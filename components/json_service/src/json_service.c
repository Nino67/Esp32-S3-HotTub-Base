#include "cJSON.h"
#include "esp_log.h"
#include "esp_crc.h"
#include <stdbool.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "json_service.h"

// #ifndef JSON_CRC32
// #endif
#define JSON_CRC32 "crc32"

/**
 * @brief  Build a JSON string with a CRC32 envelope for integrity verification
 *
 * @param json_obj The cJSON object to serialize and send
 * @param output Buffer to write the resulting JSON string with CRC32 envelope
 * @param output_size Size of the output buffer
 * @param output_len Pointer to size_t to receive the length of the resulting JSON string
 *
 * @return true if the JSON string was successfully built and fits in the output buffer, false otherwise
 */
bool crc32_json_wrapper(const cJSON *json_obj,
                        char *output,
                        size_t output_size,
                        size_t *output_len)
{
  // ESP_LOGI("json_service", "Building CRC32 enveloped JSON message");

  if (json_obj == NULL || output == NULL || output_len == NULL)
  {
    return false;
  }

  char *base_complete = cJSON_PrintUnformatted((cJSON *)json_obj);
  if (base_complete == NULL)
  {
    return false;
  }

  size_t complete_len = strlen(base_complete);
  if (complete_len < 2 || base_complete[complete_len - 1] != '}')
  {
    free(base_complete);
    return false;
  }

  size_t base_prefix_len = complete_len - 1;
  uint32_t crc = esp_crc32_le(0, (const uint8_t *)base_complete, base_prefix_len);

  // ESP_LOGI("json_service", "CRC32 value: %" PRIu32, crc);

  int written = snprintf(output,
                         output_size,
                         "%.*s,\"" JSON_CRC32 "\":%" PRIu32 "}\n",
                         (int)base_prefix_len,
                         base_complete,
                         crc);

  free(base_complete);

  if (written <= 0 || (size_t)written >= output_size)
  {
    return false;
  }

  // ESP_LOGI("json_service", "Built CRC32 enveloped JSON message: %s", output);
  *output_len = (size_t)written;
  return true;
} // end of crc32_json_wrapper()
/*=================================================================================*/




// /**
//  * @brief  Validate a received JSON string with CRC32 envelope and reconstruct the original JSON if valid
//  *
//  * @param input The input JSON string with CRC32 envelope
//  * @param output Buffer to write the reconstructed original JSON string (without CRC32 envelope)
//  * @param output_size Size of the output buffer
//  * @param expected_crc Pointer to uint32_t to receive the expected CRC32 value from the input
//  * @param computed_crc Pointer to uint32_t to receive the computed CRC32 value from the input
//  *
//  * @return true if the input is valid and the original JSON was successfully reconstructed, false otherwise
//  */
// static bool validate_root_crc_and_reconstruct_json(const char *input,
//                                                    char *output,
//                                                    size_t output_size,
//                                                    uint32_t *expected_crc,
//                                                    uint32_t *computed_crc)
// {
//   if (input == NULL || output == NULL || expected_crc == NULL || computed_crc == NULL)
//   {
//     return false;
//   }

//   size_t input_len = trim_trailing_whitespace(input, strlen(input));
//   if (input_len < 4)
//   {
//     return false;
//   }

//   static const char marker[] = ",\"" JSON_CRC32 "\":";
//   const char *search = input;
//   const char *marker_pos = NULL;

//   while (search < (input + input_len))
//   {
//     const char *candidate = strstr(search, marker);
//     if (candidate == NULL || candidate >= (input + input_len))
//     {
//       break;
//     }

//     marker_pos = candidate;
//     search = candidate + 1;
//   }

//   if (marker_pos == NULL)
//   {
//     return false;
//   }

//   const char *num_start = marker_pos + strlen(marker);
//   char *num_end = NULL;
//   unsigned long parsed = strtoul(num_start, &num_end, 10);

//   if (num_end == num_start)
//   {
//     return false;
//   }

//   if (num_end != (input + input_len - 1) || *num_end != '}')
//   {
//     return false;
//   }

//   size_t base_prefix_len = (size_t)(marker_pos - input);
//   if (base_prefix_len + 2 > output_size)
//   {
//     return false;
//   }

//   memcpy(output, input, base_prefix_len);
//   output[base_prefix_len] = '}';
//   output[base_prefix_len + 1] = '\0';

//   *expected_crc = (uint32_t)parsed;
//   *computed_crc = esp_crc32_le(0, (const uint8_t *)input, base_prefix_len);

//   return true;
// } // end of validate_root_crc_and_reconstruct_json()
// /*=================================================================================*/
