# App Watchdog

Reusable watchdog component for ESP-IDF projects that need to register arbitrary FreeRTOS tasks with the Task Watchdog Timer (TWDT) and keep a fallback RTC watchdog armed as a fail-safe reset path.

## What it does

- Initializes TWDT with a configurable timeout.
- Registers and unregisters tasks dynamically at runtime.
- Feeds TWDT from the currently running task.
- Tracks a quorum of registered tasks and feeds the RTC WDT only after every registered task has reported in.
- Keeps RTC WDT fallback enabled by default for fail-safe recovery.

## Public API

Include `app_watchdog.h` and use the following entry points:

- `app_watchdog_get_default_config()`
- `app_watchdog_init()`
- `app_watchdog_register_task()`
- `app_watchdog_register_current_task()`
- `app_watchdog_unregister_task()`
- `app_watchdog_unregister_current_task()`
- `app_watchdog_feed_task()`
- `app_watchdog_feed_current_task()`
- `app_watchdog_is_initialized()`
- `app_watchdog_is_enabled()`

## Configuration

The configuration object is `app_watchdog_config_t`:

```c
typedef enum {
    APP_WATCHDOG_QUORUM_ALL = 0,  // All registered tasks must feed (fail-safe, default)
    APP_WATCHDOG_QUORUM_ANY = 1,  // Any registered task feeding satisfies quorum
} app_watchdog_quorum_policy_t;

typedef struct {
    bool enabled;
    uint32_t twdt_timeout_s;
    uint32_t rtc_wdt_margin_ms;
    bool enable_rtc_wdt_fallback;
    bool twdt_panic_on_timeout;
    app_watchdog_quorum_policy_t quorum_policy;
} app_watchdog_config_t;
```

### Fields

- `enabled`: turns the watchdog on or off at init time. The current implementation is fail-safe oriented and expects `true` for normal use.
- `twdt_timeout_s`: TWDT timeout in seconds.
- `rtc_wdt_margin_ms`: extra margin added on top of the TWDT timeout when programming the RTC WDT fallback.
- `enable_rtc_wdt_fallback`: enables the RTC WDT backstop path.
- `twdt_panic_on_timeout`: if `true`, TWDT triggers a panic (hard reset); if `false`, allows soft timeout handling without panic.
- `quorum_policy`: controls how the RTC WDT quorum is computed:
  - `APP_WATCHDOG_QUORUM_ALL`: all registered tasks must feed before RTC WDT feeds (default, fail-safe). Good for systems where every task must stay responsive.
  - `APP_WATCHDOG_QUORUM_ANY`: RTC WDT feeds if any registered task has fed. Useful for systems with independent task sets.

### Default values

Use `app_watchdog_get_default_config()` to populate a safe default configuration:

- `enabled = true`
- `twdt_timeout_s = 5`
- `rtc_wdt_margin_ms = 2000`
- `enable_rtc_wdt_fallback = true`
- `twdt_panic_on_timeout = true` (hard reset on TWDT timeout)
- `quorum_policy = APP_WATCHDOG_QUORUM_ALL` (all tasks required)

## Registration model

Tasks are registered dynamically by handle. A task can register itself with `app_watchdog_register_current_task()` or another task can be registered by handle with `app_watchdog_register_task()`.

Example:

```c
#include "app_watchdog.h"

void my_task(void *arg)
{
    ESP_ERROR_CHECK(app_watchdog_register_current_task("my_task"));

    for (;;)
    {
        do_work();
        ESP_ERROR_CHECK(app_watchdog_feed_current_task());
    }
}
```

## Quorum behavior

The RTC WDT feeding depends on the configured quorum policy. By default (QUORUM_ALL), the RTC WDT is fed only after every currently registered task has fed at least once since the last RTC feed. That makes the quorum equivalent to "all registered tasks are healthy." 

When using QUORUM_ANY, the RTC WDT feeds as soon as any registered task has fed, which is useful for systems where tasks are independent and one task's health is sufficient.

With QUORUM_ALL, when the quorum is satisfied, the component resets the internal seen-feed bitmap and starts a new cycle. This design is intended for systems where all registered tasks must remain responsive.

## Health inspection

Use `app_watchdog_get_status()` to query the current watchdog health:

```c
app_watchdog_status_t status;
if (app_watchdog_get_status(&status) == ESP_OK) {
    printf("Watchdog initialized: %s\n", status.initialized ? "yes" : "no");
    printf("Registered tasks: %u\n", status.registered_tasks);
    printf("Tasks fed in cycle: %u / %u\n", status.fed_tasks, status.required_tasks);
}
```

## Example initialization

### Basic setup (defaults)

```c
#include "app_watchdog.h"

void start_system(void)
{
    app_watchdog_config_t config;
    app_watchdog_get_default_config(&config);

    ESP_ERROR_CHECK(app_watchdog_init(&config));
    ESP_ERROR_CHECK(app_watchdog_register_current_task("system_startup"));
}
```

### Customized: soft timeout with quorum-any

```c
#include "app_watchdog.h"

void start_system_soft_reset(void)
{
    app_watchdog_config_t config;
    app_watchdog_get_default_config(&config);
    config.twdt_timeout_s = 8;
    config.twdt_panic_on_timeout = false;        // Soft timeout, no panic
    config.quorum_policy = APP_WATCHDOG_QUORUM_ANY;  // Any task feeding is enough

    ESP_ERROR_CHECK(app_watchdog_init(&config));
    ESP_ERROR_CHECK(app_watchdog_register_current_task("system_startup"));
}
```

### Strict setup: all tasks required

```c
#include "app_watchdog.h"

void start_system_strict(void)
{
    app_watchdog_config_t config;
    app_watchdog_get_default_config(&config);
    config.twdt_timeout_s = 3;                   // Shorter timeout
    config.rtc_wdt_margin_ms = 1000;             // Tighter margin
    config.twdt_panic_on_timeout = true;         // Hard panic on TWDT
    config.quorum_policy = APP_WATCHDOG_QUORUM_ALL; // All tasks required

    ESP_ERROR_CHECK(app_watchdog_init(&config));
    ESP_ERROR_CHECK(app_watchdog_register_current_task("system_startup"));
}
```

## Example for a spawned task

```c
static TaskHandle_t s_worker_task;

static void worker_task(void *arg)
{
    ESP_ERROR_CHECK(app_watchdog_register_current_task("worker_task"));

    for (;;)
    {
        process_queue();
        ESP_ERROR_CHECK(app_watchdog_feed_current_task());
    }
}

void start_worker(void)
{
    xTaskCreate(worker_task, "worker_task", 4096, NULL, 5, &s_worker_task);
}
```

## Notes

- Register tasks after they have a valid FreeRTOS task handle.
- Feed from the same task that was registered.
- Keep the watchdog enabled for normal operation; disabling it weakens recovery behavior.
- The RTC WDT fallback is implemented with the IDF 5.5 `wdt_hal` path on this project.