#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

#define APP_WATCHDOG_TASK_NAME_MAX_LEN 32
#define APP_WATCHDOG_MAX_TASKS 8U

/**
 * @brief Quorum policy for RTC WDT feeding
 */
typedef enum
{
    APP_WATCHDOG_QUORUM_ALL = 0,  /**< All registered tasks must feed before RTC WDT feeds (default, fail-safe) */
    APP_WATCHDOG_QUORUM_ANY = 1,  /**< Any registered task feeding satisfies RTC WDT quorum */
} app_watchdog_quorum_policy_t;

/**
 * @brief Watchdog configuration
 */
typedef struct
{
    bool enabled;                              /**< Enable/disable watchdog at init time */
    uint32_t twdt_timeout_s;                   /**< TWDT timeout in seconds */
    uint32_t rtc_wdt_margin_ms;                /**< Extra margin for RTC WDT (ms) */
    bool enable_rtc_wdt_fallback;              /**< Enable RTC WDT as fail-safe backstop */
    bool twdt_panic_on_timeout;                /**< If true, TWDT triggers panic; if false, soft timeout */
    app_watchdog_quorum_policy_t quorum_policy; /**< Policy for RTC WDT quorum (all vs any) */
} app_watchdog_config_t;

/**
 * @brief Watchdog status snapshot
 */
typedef struct
{
    bool initialized;           /**< Watchdog is initialized */
    bool enabled;               /**< Watchdog is currently enabled */
    uint32_t registered_tasks;  /**< Number of currently registered tasks */
    uint32_t fed_tasks;         /**< Number of tasks that have fed in the current cycle */
    uint32_t required_tasks;    /**< Number of tasks required to satisfy quorum */
} app_watchdog_status_t;

void app_watchdog_get_default_config(app_watchdog_config_t *config);
esp_err_t app_watchdog_init(const app_watchdog_config_t *config);
esp_err_t app_watchdog_register_task(TaskHandle_t task_handle, const char *task_name);
esp_err_t app_watchdog_register_current_task(const char *task_name);
esp_err_t app_watchdog_unregister_task(TaskHandle_t task_handle);
esp_err_t app_watchdog_unregister_current_task(void);
esp_err_t app_watchdog_feed_task(TaskHandle_t task_handle);
esp_err_t app_watchdog_feed_current_task(void);
bool app_watchdog_is_initialized(void);
bool app_watchdog_is_enabled(void);
esp_err_t app_watchdog_get_status(app_watchdog_status_t *status);

#ifdef __cplusplus
}
#endif