#include <time.h>
#include "esp_log.h"
#include "esp_check.h"
#include "esp_sntp.h"
#include "esp_timer.h"
#include "app_watchdog.h"
#include "system_status.h"

#include "ntp_time_sync.h"


// ============================================================================
// DEFINES
#define DEBUG_NTP_TIME_SYNC 0

// Time maintenance task: initial NTP sync, then 1s updates and daily resync.
#define NTP_RESYNC_INTERVAL_SEC   (24 * 60 * 60)   // once per day

// Consider time "valid" if it's after 2020-01-01
#define UTILS_TIME_VALID_EPOCH 1577836800LL

// Default NTP server to use if none is provided
#define NTP_SERVER "pool.ntp.org"

#define TIME_ZONE "EST5EDT,M3.2.0,M11.1.0"   // America/Toronto

// ============================================================================
// GLOBAL STATIC VARIABLES
static const char *TAG = "ntp_time_sync";

static bool s_time_maintenance_watchdog_registered = false;

// Flag indicating if time has been synchronized
static bool s_time_synced = false;

// Clock_synchronization: initial NTP sync, if there is a network connection
static const char *tz = TIME_ZONE;   // America/Toronto
static const char *ntp_server = NULL;               // use default in utils



/**
 * @brief Initialize NTP time synchronization.
 *
 * @param ntp_server NTP server hostname (NULL for default)
 * @param tz         POSIX TZ string (NULL to leave unchanged)
 * @param timeout_ms Maximum time to wait in milliseconds
 * @return ESP_OK on success, or an error code on failure.
 */
esp_err_t ntp_utils_time_sync_nonblocking(const char *ntp_server,
                                   const char *tz,
                                   uint32_t timeout_ms)
{
    // Set timezone if provided
    if (tz != NULL)
    {
        setenv("TZ", tz, 1);
        tzset();
    }

    // Initialize SNTP
    if (ntp_server == NULL)
    {
        ntp_server = NTP_SERVER;
    }

    if (esp_sntp_enabled()) {
        esp_sntp_stop();
    }

    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, ntp_server);
    esp_sntp_init();

    return ESP_OK;
}




// ============================================================================
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
                                   uint32_t timeout_ms)
{
    // Set timezone if provided
    if (tz != NULL)
    {
        setenv("TZ", tz, 1);
        tzset();
    }

    // Initialize SNTP
    if (ntp_server == NULL)
    {
        ntp_server = NTP_SERVER;
    }

    if (esp_sntp_enabled()) {
        esp_sntp_stop();
    }

    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, ntp_server);
    esp_sntp_init();

    // Wait for time to be set
    const int64_t start = esp_timer_get_time() / 1000; // ms

    while (true)
    {
        // Get current time
        time_t now = 0;
        time(&now);

        // Check if time is valid, set flag and return 
        if (now >= UTILS_TIME_VALID_EPOCH)
        {
            s_time_synced = true;
            if(DEBUG_NTP_TIME_SYNC) {
                ESP_LOGI(TAG, "Time synchronized: %ld", (long)now);
            }
            return ESP_OK;
        }

        // Check for timeout, return error if exceeded 
        int64_t elapsed = (esp_timer_get_time() / 1000) - start;
        if (elapsed > timeout_ms)
        {
            ESP_LOGW(TAG, "Time sync timeout after %ld ms", (long)elapsed);
            return ESP_ERR_TIMEOUT;
        }

        vTaskDelay(pdMS_TO_TICKS(500));
    }

} // end of utils_time_sync_blocking(ntp_server, tz, timeout_ms)
// ============================================================================



/**
 * @brief Get current local time as struct tm
 * 
 * @param out_time Pointer to struct tm to receive local time
 * 
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if out_time is NULL,
 *         ESP_ERR_INVALID_STATE if time not synchronized, or ESP_FAIL on failure
 * 
 * @note Get current local time. Returns ESP_OK on success, ESP_ERR_INVALID_STATE
 * if time has not been synchronized yet, or ESP_ERR_INVALID_ARG / ESP_FAIL
 * for invalid parameters or internal failures.
 * Caller must provide valid pointer for out_time.
 */
esp_err_t ntp_utils_time_get_local(struct tm *out_time)
{
    // Validate output parameter
    if (out_time == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    // Retrieve current time
    time_t now = 0;
    time(&now);

    // Check if time has been synchronized and is valid
    if (!s_time_synced || now < UTILS_TIME_VALID_EPOCH)
    {
        return ESP_ERR_INVALID_STATE;
    }

    // Convert to local time
    if (localtime_r(&now, out_time) == NULL)
    {
        return ESP_FAIL;
    }

    return ESP_OK;

} // end of utils_time_get_local(out_time)
// ============================================================================



/**
 * @brief Get current local time as ISO8601 string (e.g. "2025-11-29T12:34:56+0100").
 * 
 * @param buf Pointer to buffer to receive ISO8601 string
 * @param len Length of buffer in bytes
 * 
 * @return ESP_OK on success, or error code from utils_time_get_local / strftime
 */
esp_err_t ntp_utils_time_get_iso8601(char *buf, size_t len)
{
    // Validate input parameters
    if (buf == NULL || len == 0)
    {
        return ESP_ERR_INVALID_ARG;
    }

    // Get current local time
    struct tm tm_now;
    esp_err_t err = ntp_utils_time_get_local(&tm_now);
    if (err != ESP_OK)
    {
        return err;
    }

    // Format as ISO8601 string
    if (strftime(buf, len, "%Y-%m-%dT%H:%M:%S%z", &tm_now) == 0)
    {
        return ESP_FAIL;
    }

    return ESP_OK;

} // end of utils_time_get_iso8601(buf, len)
// ============================================================================



/**
 * @brief Initialize NTP time synchronization (blocking)
 * 
 * @return ESP_OK on success, or error code from utils_time_sync_blocking
 * 
 * @note Perform initial NTP time synchronization in a blocking manner.
 */
esp_err_t ntp_time_sync_init(void)
{
    static time_t last_sync = 0;
    if(DEBUG_NTP_TIME_SYNC) {
        ESP_LOGI(TAG, "NTP time synchronization: started");
    }
    while (last_sync == 0) {
        if(DEBUG_NTP_TIME_SYNC) {
            ESP_LOGI(TAG, "***** NTP time synchronization: performing initial NTP sync");
        }
        // esp_err_t r = ntp_utils_time_sync_blocking(ntp_server, tz, 15000);
        esp_err_t r = ntp_utils_time_sync_nonblocking(ntp_server, tz, 15000);
      
        if (r == ESP_OK) {
            time(&last_sync);
            if(DEBUG_NTP_TIME_SYNC) {
                ESP_LOGI(TAG, "**** NTP time synchronization: initial NTP sync OK");
            }
        } else {
            ESP_LOGW(TAG, "NTP time synchronization: initial NTP sync failed: %s", esp_err_to_name(r));
        }
        if (last_sync == 0) {
            if(DEBUG_NTP_TIME_SYNC) {
                ESP_LOGI(TAG, "NTP time synchronization: retrying NTP sync in 10 seconds");
            }
            vTaskDelay(pdMS_TO_TICKS(10000));
        }
    }

    return ESP_OK;
} // end of ntp_time_sync_init()
// ============================================================================




static void time_maintenance_register_watchdog(void)
{
    if (s_time_maintenance_watchdog_registered || !app_watchdog_is_initialized()) {
        return;
    }

    esp_err_t err = app_watchdog_register_current_task("time_maintenance_task");
    if (err == ESP_OK) {
        s_time_maintenance_watchdog_registered = true;
        ESP_LOGI(TAG, "time_maintenance_task registered with watchdog");
    } else {
        ESP_LOGW(TAG, "time_maintenance_task watchdog registration failed: %s", esp_err_to_name(err));
    }
}


/**
 * @brief Time maintenance task
 *
 * This task performs periodic NTP time synchronization and feeds the watchdog.
 * It ensures the system time remains accurate and prevents watchdog resets.
 * 
 * @param arg Task argument (unused)
 */
void time_maintenance_task(void *arg)
{
    const char *tz = TIME_ZONE;   // America/Toronto
    const char *ntp_server = NULL;               // use default in utils
    time_t last_sync = 0;

    ESP_LOGI(TAG, "time_maintenance_task: starting initial NTP sync");
    // esp_err_t err = ntp_utils_time_sync_blocking(ntp_server, tz, 15000);
    esp_err_t err = ntp_utils_time_sync_blocking(ntp_server, tz, 15000);
    if (err == ESP_OK) {
        time(&last_sync);
        ESP_LOGI(TAG, "time_maintenance_task: initial NTP sync OK");
    } else {
        ESP_LOGW(TAG, "time_maintenance_task: initial NTP sync failed: %s", esp_err_to_name(err));
    }

    time_maintenance_register_watchdog();

    while (1) {
        // 1-second tick
        vTaskDelay(pdMS_TO_TICKS(1000));

        if (!s_time_maintenance_watchdog_registered) {
            time_maintenance_register_watchdog();
        }

        // Feed watchdog to prevent reset
        if (s_time_maintenance_watchdog_registered) {
            if (app_watchdog_feed_current_task() != ESP_OK)
            {
                ESP_LOGW(TAG, "time maintenance task failed to feed watchdog");
            }
        }

        // Update periodic system status fields
        esp_err_t status_err = system_status_update();
        if (status_err != ESP_OK) {
            ESP_LOGW(TAG, "system_status_update failed: %s", esp_err_to_name(status_err));
        }

        // Once per day, resync via NTP to correct drift
        time_t now = 0;
        time(&now);

        if (last_sync == 0 || (now - last_sync) >= NTP_RESYNC_INTERVAL_SEC) {
            ESP_LOGI(TAG, "time_maintenance_task: performing daily NTP resync");
            esp_err_t r = ntp_utils_time_sync_blocking(ntp_server, tz, 15000);
            if (r == ESP_OK) {
                last_sync = now;
                ESP_LOGI(TAG, "time_maintenance_task: daily NTP resync OK");
            } else {
                ESP_LOGW(TAG, "time_maintenance_task: daily NTP resync failed: %s", esp_err_to_name(r));
            }
        }
    }
}
// ============================================================================






// // Inside your merged heartbeat / housekeeping task:

// const char *tz = "EST5EDT,M3.2.0,M11.1.0";   // America/Toronto
// const char *ntp_server = NULL;               
// bool initial_sync_done = false;

// // A simple countdown timer initialized to 0 so it attempts a sync immediately if needed
// uint32_t ntp_countdown_sec = 0; 

// while (1) {
//     // 1. Toggle your heartbeat LED every second
//     rgb_led_heartbeat_toggle();

//     // 2. Handle the NTP maintenance countdown
//     if (ntp_countdown_sec > 0) {
//         ntp_countdown_sec--;
//     }

//     // If the countdown hits 0, it's time to sync (either initial or daily resync)
//     if (ntp_countdown_sec == 0) {
//         ESP_LOGI(TAG, "Housekeeping: Requesting NTP time sync...");
        
//         // Note: Since this is running in a 1-second loop, a long blocking sync 
//         // will stall your LED toggle for 15 seconds if it times out. 
//         // If that bothers you down the road, you can use a non-blocking/async ntp call.
//         esp_err_t r = ntp_utils_time_sync_blocking(ntp_server, tz, 15000);
        
//         if (r == ESP_OK) {
//             ESP_LOGI(TAG, "Housekeeping: NTP sync OK.");
//             initial_sync_done = true;
//             // Sync succeeded! Lock it down for exactly 24 hours (86400 seconds)
//             ntp_countdown_sec = 24 * 60 * 60; 
//         } else {
//             ESP_LOGW(TAG, "Housekeeping: NTP sync failed: %s", esp_err_to_name(r));
            
//             // If it's the initial boot sync that failed, retry soon (e.g., in 30 seconds)
//             // If it's just a daily drift update failing, retry in an hour.
//             ntp_countdown_sec = (!initial_sync_done) ? 30 : 3600;
//         }
//     }

//     // 3. Keep the watchdog happy
//     app_watchdog_feed_current_task();

//     // Exact 1-second loop cadence
//     vTaskDelay(pdMS_TO_TICKS(1000));
// }