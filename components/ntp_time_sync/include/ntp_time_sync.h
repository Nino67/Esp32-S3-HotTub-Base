

#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include "esp_err.h"

#ifndef _ntp_time_sync_h_
#define _ntp_time_sync_h_


#ifdef __cplusplus
extern "C"
{
#endif


#define NTP_RESYNC_INTERVAL_SEC   (24 * 60 * 60)   // once per day


/**
 * @brief Initialize NTP time synchronization (blocking)
 * 
 * @return ESP_OK on success, or error code from utils_time_sync_blocking
 * 
 * @note Perform initial NTP time synchronization in a blocking manner.
 */
esp_err_t ntp_time_sync_init(void);


/**
 * @brief Time maintenance task: initial NTP sync, then 1s updates and daily resync.
 * 
 * @param arg Task argument (unused)
 */
void time_maintenance_task(void *arg);


/** 
 * @brief Synchronize system time via NTP (blocking)
 * 
 * @param ntp_server NTP server hostname (NULL for default)
 * @param tz         POSIX TZ string (NULL to leave unchanged)
 * @param timeout_ms Maximum time to wait in milliseconds
 * @return ESP_OK on success, ESP_ERR_TIMEOUT on timeout, or other error
 * 
 * @note Block until SNTP sets system time or timeout.
 * - ntp_server: NTP server hostname (e.g. "pool.ntp.org"); if NULL, a
 *      reasonable default will be used.
 * - tz: POSIX TZ string (e.g. "EST5EDT,M3.2.0,M11.1.0");
 *      if NULL, the existing TZ is left unchanged.
 * - timeout_ms: maximum time to wait for a valid time.
 */
esp_err_t ntp_utils_time_sync_blocking(const char *ntp_server,
								   const char *tz,
								   uint32_t timeout_ms);



/**
 * @brief Get current local time as struct tm
 * 
 * @param out_time Pointer to struct tm to receive local time
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if out_time is NULL,
 *         ESP_ERR_INVALID_STATE if time not synchronized, or ESP_FAIL on failure
 * 
 * @note Get current local time. Returns ESP_OK on success, ESP_ERR_INVALID_STATE
 * if time has not been synchronized yet, or ESP_ERR_INVALID_ARG / ESP_FAIL
 * for invalid parameters or internal failures.
 * Caller must provide valid pointer for out_time.
 */
esp_err_t ntp_utils_time_get_local(struct tm *out_time);


/**
 * @brief Get current local time as ISO8601 string (e.g. "2025-11-29T12:34:56+0100").
 * 
 * @param buf Pointer to buffer to receive ISO8601 string
 * @param len Length of buffer in bytes
 * 
 * @return ESP_OK on success, or error code from utils_time_get_local / strftime
 */
esp_err_t ntp_utils_time_get_iso8601(char *buf, size_t len);

// ============================================================================

#ifdef __cplusplus
}
#endif


#endif // _ntp_time_sync_h_