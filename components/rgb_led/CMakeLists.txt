idf_component_register(SRCS "src/rgb_led.c"
                       INCLUDE_DIRS "include"
                       REQUIRES led_strip app_watchdog)
