# Hot Tub Controller

ESP32-S3-WROOM-1 N16R8 base layer project built on ESP-IDF and FreeRTOS.

Included in this first pass:

- dual OTA partitioning with rollback support
- event-driven Wi-Fi manager
- NimBLE BLE service with secure pairing support
- HTTP server with WebSocket endpoint
- LittleFS-backed static web UI

Build notes:

- set the ESP-IDF target to `esp32s3n16r8`
- build with `idf.py build`
- flash with `idf.py flash monitor`

The project currently starts in STA mode for local access and can be extended to AP and credentials later.
