#include "app_watchdog.h"

#include <inttypes.h>
#include <string.h>

#include "esp_idf_version.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "hal/wdt_hal.h"
#include "freertos/semphr.h"
#include "soc/rtc.h"

static const char *TAG = "app_watchdog";

#define APP_WATCHDOG_MAX_TASKS 8U

typedef struct
{
    TaskHandle_t handle;
    bool active;
    bool subscribed;
    char name[APP_WATCHDOG_TASK_NAME_MAX_LEN];
} app_watchdog_task_slot_t;

static SemaphoreHandle_t s_mutex;
static bool s_initialized;
static bool s_enabled;
static app_watchdog_config_t s_config;
static app_watchdog_task_slot_t s_slots[APP_WATCHDOG_MAX_TASKS];
static uint32_t s_required_feed_mask;
static uint32_t s_seen_feed_mask;
static wdt_hal_context_t s_rtc_wdt_ctx;

static void set_default_config(app_watchdog_config_t *config)
{
    if (!config)
    {
        return;
    }

    config->enabled = true;
    config->twdt_timeout_s = 5U;
    config->rtc_wdt_margin_ms = 2000U;
    config->enable_rtc_wdt_fallback = true;
    config->twdt_panic_on_timeout = true;
    config->quorum_policy = APP_WATCHDOG_QUORUM_ALL;
}

void app_watchdog_get_default_config(app_watchdog_config_t *config)
{
    set_default_config(config);
}

static esp_err_t configure_twdt(const app_watchdog_config_t *config)
{
    if (!config->enabled)
    {
        return ESP_OK;
    }

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
    esp_task_wdt_config_t twdt_config = {
        .timeout_ms = config->twdt_timeout_s * 1000U,
        .idle_core_mask = (1U << portNUM_PROCESSORS) - 1U,
        .trigger_panic = true,
    };

    esp_err_t ret = esp_task_wdt_reconfigure(&twdt_config);
    if (ret == ESP_ERR_INVALID_STATE)
    {
        ret = esp_task_wdt_init(&twdt_config);
    }
#else
    esp_err_t ret = esp_task_wdt_init(config->twdt_timeout_s, true);
    if (ret == ESP_ERR_INVALID_STATE)
    {
        ret = ESP_OK;
    }
#endif

    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to configure TWDT: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG,
             "TWDT configured: timeout=%" PRIu32 "s, panic/reboot on timeout enabled",
             config->twdt_timeout_s);
    return ESP_OK;
}

static esp_err_t configure_rtc_wdt(const app_watchdog_config_t *config)
{
    if (!config->enabled || !config->enable_rtc_wdt_fallback)
    {
        return ESP_OK;
    }

    const uint64_t rtc_timeout_ms = (uint64_t)config->twdt_timeout_s * 1000ULL + config->rtc_wdt_margin_ms;
    const uint64_t slow_clk_hz = rtc_clk_slow_freq_get_hz();
    if (slow_clk_hz == 0)
    {
        ESP_LOGE(TAG, "RTC slow clock frequency is invalid");
        return ESP_FAIL;
    }

    const uint32_t stage_timeout_ticks = (uint32_t)((rtc_timeout_ms * slow_clk_hz) / 1000ULL);

    wdt_hal_init(&s_rtc_wdt_ctx, WDT_RWDT, 0, false);

    wdt_hal_write_protect_disable(&s_rtc_wdt_ctx);
    wdt_hal_disable(&s_rtc_wdt_ctx);
    wdt_hal_config_stage(&s_rtc_wdt_ctx, WDT_STAGE0, stage_timeout_ticks, WDT_STAGE_ACTION_RESET_SYSTEM);
    wdt_hal_set_flashboot_en(&s_rtc_wdt_ctx, true);
    wdt_hal_enable(&s_rtc_wdt_ctx);
    wdt_hal_feed(&s_rtc_wdt_ctx);
    wdt_hal_write_protect_enable(&s_rtc_wdt_ctx);

    ESP_LOGI(TAG,
             "RTC WDT fallback configured: timeout=%" PRIu64 "ms",
             rtc_timeout_ms);
    return ESP_OK;
}

static void refresh_required_feed_mask_locked(void)
{
    uint32_t required_mask = 0;

    for (size_t i = 0; i < APP_WATCHDOG_MAX_TASKS; ++i)
    {
        if (s_slots[i].active && s_slots[i].handle != NULL)
        {
            required_mask |= (1U << i);
        }
    }

    s_required_feed_mask = required_mask;
    s_seen_feed_mask &= s_required_feed_mask;
}

static void feed_rtc_wdt_if_quorum_locked(void)
{
    if (!s_enabled || !s_config.enable_rtc_wdt_fallback || s_required_feed_mask == 0)
    {
        return;
    }

    bool quorum_satisfied = false;

    if (s_config.quorum_policy == APP_WATCHDOG_QUORUM_ALL)
    {
        quorum_satisfied = (s_seen_feed_mask & s_required_feed_mask) == s_required_feed_mask;
    }
    else if (s_config.quorum_policy == APP_WATCHDOG_QUORUM_ANY)
    {
        quorum_satisfied = (s_seen_feed_mask & s_required_feed_mask) != 0;
    }

    if (quorum_satisfied)
    {
        wdt_hal_write_protect_disable(&s_rtc_wdt_ctx);
        wdt_hal_feed(&s_rtc_wdt_ctx);
        wdt_hal_write_protect_enable(&s_rtc_wdt_ctx);
        s_seen_feed_mask = 0;
    }
}

static app_watchdog_task_slot_t *find_slot_by_handle(TaskHandle_t task_handle)
{
    if (!task_handle)
    {
        return NULL;
    }

    for (size_t i = 0; i < APP_WATCHDOG_MAX_TASKS; ++i)
    {
        if (s_slots[i].active && s_slots[i].handle == task_handle)
        {
            return &s_slots[i];
        }
    }

    return NULL;
}

static app_watchdog_task_slot_t *alloc_slot(void)
{
    for (size_t i = 0; i < APP_WATCHDOG_MAX_TASKS; ++i)
    {
        if (!s_slots[i].active)
        {
            return &s_slots[i];
        }
    }

    return NULL;
}

static void unsubscribe_task_locked(app_watchdog_task_slot_t *slot)
{
    if (!slot || !slot->active || !slot->subscribed || !s_enabled)
    {
        return;
    }

    esp_err_t ret = esp_task_wdt_delete(slot->handle);
    if (ret == ESP_OK)
    {
        slot->subscribed = false;
        ESP_LOGI(TAG, "Unsubscribed task from TWDT: %s", slot->name);
    }
    else
    {
        ESP_LOGW(TAG, "Failed to unsubscribe %s from TWDT: %s", slot->name, esp_err_to_name(ret));
    }
}

static esp_err_t subscribe_task_locked(app_watchdog_task_slot_t *slot)
{
    if (!slot || !slot->active || !slot->handle || !s_enabled || slot->subscribed)
    {
        return ESP_OK;
    }

    esp_err_t ret = esp_task_wdt_add(slot->handle);
    if (ret == ESP_OK)
    {
        slot->subscribed = true;
        ESP_LOGI(TAG, "Subscribed task to TWDT: %s", slot->name);
        return ESP_OK;
    }

    ESP_LOGE(TAG, "Failed to subscribe %s to TWDT: %s", slot->name, esp_err_to_name(ret));
    return ret;
}

static esp_err_t register_task_locked(TaskHandle_t task_handle, const char *task_name)
{
    if (!task_handle)
    {
        return ESP_ERR_INVALID_ARG;
    }

    app_watchdog_task_slot_t *slot = find_slot_by_handle(task_handle);
    if (!slot)
    {
        slot = alloc_slot();
        if (!slot)
        {
            ESP_LOGE(TAG, "No free watchdog task slots available");
            return ESP_ERR_NO_MEM;
        }

        memset(slot, 0, sizeof(*slot));
        slot->active = true;
        slot->handle = task_handle;
    }

    if (task_name && task_name[0] != '\0')
    {
        strlcpy(slot->name, task_name, sizeof(slot->name));
    }
    else if (slot->name[0] == '\0')
    {
        strlcpy(slot->name, "task", sizeof(slot->name));
    }

    esp_err_t ret = subscribe_task_locked(slot);
    refresh_required_feed_mask_locked();
    return ret;
}

esp_err_t app_watchdog_init(const app_watchdog_config_t *config)
{
    if (!s_mutex)
    {
        s_mutex = xSemaphoreCreateMutex();
        if (!s_mutex)
        {
            ESP_LOGE(TAG, "Failed to create watchdog mutex");
            return ESP_ERR_NO_MEM;
        }
    }

    app_watchdog_config_t effective_config;
    set_default_config(&effective_config);
    if (config)
    {
        effective_config = *config;
    }

    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) != pdTRUE)
    {
        return ESP_ERR_TIMEOUT;
    }

    if (s_initialized)
    {
        xSemaphoreGive(s_mutex);
        return ESP_OK;
    }

    esp_err_t ret = configure_twdt(&effective_config);
    if (ret == ESP_OK)
    {
        ret = configure_rtc_wdt(&effective_config);
    }

    if (ret == ESP_OK)
    {
        s_config = effective_config;
        s_enabled = effective_config.enabled;
        s_initialized = true;
        refresh_required_feed_mask_locked();
        ESP_LOGI(TAG, "App watchdog initialized: enabled=%s", s_enabled ? "true" : "false");
    }

    xSemaphoreGive(s_mutex);
    return ret;
}

esp_err_t app_watchdog_register_task(TaskHandle_t task_handle, const char *task_name)
{
    if (!s_initialized)
    {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) != pdTRUE)
    {
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t ret = register_task_locked(task_handle, task_name);
    xSemaphoreGive(s_mutex);
    return ret;
}

esp_err_t app_watchdog_register_current_task(const char *task_name)
{
    return app_watchdog_register_task(xTaskGetCurrentTaskHandle(), task_name);
}

esp_err_t app_watchdog_unregister_task(TaskHandle_t task_handle)
{
    if (!s_initialized)
    {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) != pdTRUE)
    {
        return ESP_ERR_TIMEOUT;
    }

    app_watchdog_task_slot_t *slot = find_slot_by_handle(task_handle);
    if (!slot)
    {
        xSemaphoreGive(s_mutex);
        return ESP_ERR_NOT_FOUND;
    }

    unsubscribe_task_locked(slot);
    memset(slot, 0, sizeof(*slot));
    refresh_required_feed_mask_locked();

    xSemaphoreGive(s_mutex);
    return ESP_OK;
}

esp_err_t app_watchdog_unregister_current_task(void)
{
    return app_watchdog_unregister_task(xTaskGetCurrentTaskHandle());
}

esp_err_t app_watchdog_feed_task(TaskHandle_t task_handle)
{
    if (!s_initialized)
    {
        return ESP_ERR_INVALID_STATE;
    }

    if (!s_enabled)
    {
        return ESP_OK;
    }

    if (!task_handle)
    {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t twdt_ret = esp_task_wdt_reset();

    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(20)) == pdTRUE)
    {
        app_watchdog_task_slot_t *slot = find_slot_by_handle(task_handle);
        if (!slot)
        {
            xSemaphoreGive(s_mutex);
            return ESP_ERR_NOT_FOUND;
        }

        s_seen_feed_mask |= (1U << (slot - s_slots));
        feed_rtc_wdt_if_quorum_locked();
        xSemaphoreGive(s_mutex);
    }

    return twdt_ret;
}

esp_err_t app_watchdog_feed_current_task(void)
{
    return app_watchdog_feed_task(xTaskGetCurrentTaskHandle());
}

bool app_watchdog_is_initialized(void)
{
    return s_initialized;
}

bool app_watchdog_is_enabled(void)
{
    return s_enabled;
}

esp_err_t app_watchdog_get_status(app_watchdog_status_t *status)
{
    if (!status)
    {
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) != pdTRUE)
    {
        return ESP_ERR_TIMEOUT;
    }

    status->initialized = s_initialized;
    status->enabled = s_enabled;
    status->registered_tasks = 0;
    status->fed_tasks = 0;

    for (size_t i = 0; i < APP_WATCHDOG_MAX_TASKS; ++i)
    {
        if (s_slots[i].active)
        {
            status->registered_tasks++;
            if (s_seen_feed_mask & (1U << i))
            {
                status->fed_tasks++;
            }
        }
    }

    if (s_config.quorum_policy == APP_WATCHDOG_QUORUM_ALL)
    {
        status->required_tasks = status->registered_tasks;
    }
    else
    {
        status->required_tasks = (status->registered_tasks > 0) ? 1 : 0;
    }

    xSemaphoreGive(s_mutex);
    return ESP_OK;
}