# ntp_time_sync Component

Back to project guide: [../../README.md](../../README.md)

## What This Component Does

`ntp_time_sync` keeps device time accurate using NTP when network is available.

## Time Sync Flow

```mermaid
flowchart LR
  Boot --> NetworkReady[Wi-Fi connected]
  NetworkReady --> NTP[Query NTP server]
  NTP --> Clock[Set system clock]
  Clock --> Timestamp[Use in status/logs]
```

## Beginner Explanation

Without NTP, timestamps are often wrong after reboot. This component gives meaningful event time in logs and JSON status.

## Troubleshooting

- If time is 1970, network or NTP query failed.
- Verify DNS/internet connectivity in STA mode.
