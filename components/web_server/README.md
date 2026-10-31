# web_server Component

Back to project guide: [../../README.md](../../README.md)

## What This Component Does

`web_server` serves static UI files from LittleFS and handles WebSocket JSON traffic.

## HTTP + WebSocket Flow

```mermaid
flowchart LR
  Browser -->|HTTP GET| WebServer[web_server]
  WebServer -->|LittleFS files| Browser
  Browser -->|WebSocket JSON| WebServer
  WebServer -->|dispatch| json_service
```

## Beginner Explanation

The web page you open in a browser is hosted by this component. After page load, real-time updates happen through WebSocket.

## Practical Steps

1. Connect device and browser to same network.
2. Open board IP in browser.
3. Watch JSON status updates and send commands.

When STA has failed and the recovery AP is active, connect to the `HotTub-Controller` Wi-Fi network and open `http://192.168.4.1/`. The web server is started independently of STA/AP mode, so it remains available through the recovery AP.

## Troubleshooting

- If page loads but no live data, inspect WebSocket connection path.
- If static files are stale, reflash LittleFS assets.
- If STA credentials fail repeatedly, use the recovery AP at `192.168.4.1` and provision working credentials over BLE.
