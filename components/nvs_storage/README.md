# nvs_storage Component

Back to project guide: [../../README.md](../../README.md)

## What This Component Does

`nvs_storage` manages persistent key/value data in flash memory.

Saved items include boot-failure counts and the Wi-Fi SSID/password pair used after reboot.

Wi-Fi credentials are stored together as one versioned NVS blob so the SSID and password are committed as a pair. The credentials are read before the build-time Wi-Fi fallback during application startup.

## Storage Model

```mermaid
flowchart TD
  App[Runtime settings] --> Save[Write key/value to NVS]
  Reboot --> Load[Read key/value from NVS]
  Load --> App
```

## Beginner Explanation

NVS is like a small on-device database for configuration.

## Troubleshooting

- If settings reset every boot, check init errors and key namespaces.
- If writes fail, inspect flash partition and return codes.
