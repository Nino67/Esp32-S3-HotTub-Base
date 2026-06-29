#pragma once

#include "led_strip.h"

#ifndef HEARTBEAT_INTERVAL_MS
#define HEARTBEAT_INTERVAL_MS 1000
#endif

#ifndef OTA_HEARTBEAT_INTERVAL_MS
#define OTA_HEARTBEAT_INTERVAL_MS 20
#endif

esp_err_t cycle_rgb_led_colors(void);
esp_err_t deinit_rgb_led(void);
esp_err_t clear_rgb_led(void);
esp_err_t set_rgb_led_color(uint8_t red, uint8_t green, uint8_t blue);
esp_err_t init_rgb_led_pin(gpio_num_t gpio_num);
esp_err_t init_rgb_led(void);
esp_err_t set_heartbeat_interval(int interval_ms);
esp_err_t rgb_led_heartbeat(void);
// void heartbeat_loop_task(void *arg);
