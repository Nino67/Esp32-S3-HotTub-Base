#pragma once


#ifndef HOT_TUB_CONTROLLER_NVS_H
#define HOT_TUB_CONTROLLER_NVS_H

#include "esp_err.h"

esp_err_t hot_tub_controller_persistence_init(void);
esp_err_t hot_tub_controller_persistence_mark_dirty(void);
esp_err_t hot_tub_controller_settings_save_to_nvs(void);
esp_err_t hot_tub_controller_settings_load_from_nvs(void);











#endif // HOT_TUB_CONTROLLER_NVS_H


