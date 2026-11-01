# wifi_manager Component

Back to project guide: [../../README.md](../../README.md)

## What This Component Does

`wifi_manager` starts and monitors Wi-Fi in one of two modes:
- STA mode (connect to existing router)
- AP fallback mode (board hosts its own network)

It tracks STA/AP IP info, publishes wireless status into `system_status`, and accepts BLE-triggered credential trials without blocking the BLE receive callback.

## Mode Decision Flow

```mermaid
flowchart TD
  Start --> HasCreds{SSID loaded from NVS or build config?}
  HasCreds -->|Yes| STA[Start STA mode]
  HasCreds -->|No| AP[Start AP mode]
  STA --> GotIP[IP_EVENT_STA_GOT_IP]
  GotIP --> PublishSTA[Update system status with SSID + STA IP]
  STA --> Failures{5 consecutive attempts fail?}
  Failures -->|No| STA
  Failures -->|Yes| AP
  AP --> PublishAP[Update system status with AP SSID and IP]
  BLE[BLE credentials request] --> Trial[Try candidate asynchronously]
  Trial --> TrialOK{Candidate gets IP?}
  TrialOK -->|Yes| Save[Save credential pair to NVS]
  TrialOK -->|No| Restore[Restore old STA or remain in AP fallback]
```

## Beginner Explanation

If your home Wi-Fi credentials exist, the board joins your router. If not, it starts its own hotspot.

NVS-provisioned credentials take precedence over build-time credentials. STA makes five total consecutive connection attempts; a successful IP acquisition resets the counter. After five failures, the manager switches to AP-only mode (the recovery AP).

While the recovery AP is active and saved credentials exist, the manager retries the saved network every 60 s without a reboot, so a router that boots later than the controller (for example after a power cut) is picked up automatically. A retry briefly runs AP+STA and is skipped while any client is connected to the recovery AP, because joining the router moves the AP to the router's channel. If the retry fails the device simply stays in recovery AP mode; if it succeeds the recovery AP stops and STA mode resumes. A successful BLE credential update also returns the device to STA mode.

Wi-Fi uses 20 MHz channels (more reliable than 40 MHz on a crowded 2.4 GHz band shared with BLE), and the STA hostname is `hottub`.

The recovery AP uses the fixed `192.168.4.1/24` address, with gateway `192.168.4.1` and DHCP enabled. If the device entered AP fallback after STA failures, a failed BLE credential trial returns to AP mode and retains the prior saved credentials. A successful trial persists the new pair and returns to STA mode.

## Example Status Fields

- `current_mode`
- `active_ssid`
- `ip_address` (the device's own STA or AP IP)
- `connected_client_count` and `clients[]` (MAC and RSSI of recovery AP clients; IPs are not reported)

## Troubleshooting

- STA never connects: check credentials and signal.
- AP missing: verify AP config and channel/auth setup.
