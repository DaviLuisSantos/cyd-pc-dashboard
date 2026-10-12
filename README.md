# CYD PC Dashboard

[![build](https://github.com/DaviLuisSantos/cyd-pc-dashboard/actions/workflows/build.yml/badge.svg)](https://github.com/DaviLuisSantos/cyd-pc-dashboard/actions/workflows/build.yml)
![platform](https://img.shields.io/badge/platform-ESP32-blue)
![license](https://img.shields.io/badge/license-MIT-green)

A touch-screen **desk companion for your PC**, built on the ~US$15 **ESP32-2432S028R** ("Cheap Yellow Display", 2.8" ILI9341 + XPT2046 touch). It sits next to your monitor and shows, at a glance, what you'd otherwise alt-tab for: CPU/RAM/disk, top processes, network health, market quotes and YouTube channel stats.

<!-- Add a photo of the panel here: ![CYD PC Dashboard](docs/panel.jpg) -->

## Features

| Tab | What it shows |
| --- | --- |
| **Clock** | Time/date via NTP, blinking colon, seconds progress bar |
| **PC** | Animated ring gauges for CPU and RAM, CPU history sparkline, disk usage bar, live network throughput |
| **Processes** | Top 4 processes by CPU (instances aggregated by name) with memory usage |
| **Network** | Latency (TCP ping), PC uptime, public IP, download history sparkline |
| **Quotes** | Any fiat currency or crypto, in any base currency, with rolling price animation and daily % change badge |
| **YouTube** | Subscribers / views / video count for up to 3 channels |

- **Dracula theme**, anti-aliased shapes and fonts, 100% vector icons (no SPIFFS/LittleFS, no bitmaps)
- **Tab navigation** by touch + optional auto-rotation between tabs
- **Flicker-free rendering**: only changed regions are redrawn, via off-screen sprites
- **Configurable** without touching the code: WiFi, agent host, timezone, language (EN / PT-BR), which tabs are shown, display rotation/inversion, brightness, touch calibration
- **Agent-based architecture**: the ESP32 only speaks plain HTTP on your LAN; a small Python agent on the PC collects metrics and caches external APIs (no API keys or HTTPS on the microcontroller)

## Architecture

```
┌────────────────────┐   HTTP/JSON (LAN)   ┌────────────────────────┐   HTTPS
│  ESP32 CYD panel   │ ──────────────────► │  pc_stats_agent.py     │ ───────► AwesomeAPI (fiat)
│  (PlatformIO, C++) │  /stats /procs      │  psutil + stdlib only  │ ───────► CoinGecko (crypto)
│                    │  /sysinfo /quotes   │  background threads,   │ ───────► YouTube Data API
│                    │  /youtube           │  in-memory cache       │ ───────► ipify (public IP)
└────────────────────┘                     └────────────────────────┘
```

The agent polls everything in background threads and the HTTP handlers only read from cache, so the panel can poll every couple of seconds without hammering external APIs.

## Hardware

- ESP32-2432S028R (Cheap Yellow Display) — the one with the resistive touch
- USB cable (the board uses a **CH340** USB-serial chip; install its driver if the port doesn't show up)

## Getting started

### 1. PC agent

Requires Python 3.8+.

```bash
pip install -r agent/requirements.txt
cp agent/config.example.json agent/config.json   # then edit it
python agent/pc_stats_agent.py
```

Check it in a browser: `http://localhost:8377/stats`. Allow port **8377** on your firewall for the private network.

On Windows you can double-click `agent/start_agent.bat`, `stop_agent.bat` and `restart_agent.bat`. To start it automatically on login, create a Task Scheduler task running `pythonw.exe agent\pc_stats_agent.py` (or a systemd user service / launchd agent on Linux/macOS).

### 2. Firmware

Install [PlatformIO](https://platformio.org/) (the VS Code extension is the easiest way), then:

```bash
cp src/user_config.example.h src/user_config.h   # then edit it
pio run -t upload
pio device monitor                               # optional, serial @ 115200
```

At minimum set `WIFI_SSID`, `WIFI_PASSWORD` and `AGENT_HOST` (the LAN IP of the PC running the agent).

## Configuration

### Firmware — `src/user_config.h`

Only define what you want to change; every other value falls back to the defaults in [`src/config.h`](src/config.h). This file is git-ignored.

| Option | Default | Description |
| --- | --- | --- |
| `WIFI_SSID` / `WIFI_PASSWORD` | — | WiFi credentials |
| `AGENT_HOST` / `AGENT_PORT` | `192.168.0.100` / `8377` | Where the agent is running |
| `PANEL_LANG` | `LANG_EN` | UI language: `LANG_EN` or `LANG_PT_BR` |
| `TZ_INFO` | `UTC0` | POSIX timezone string, e.g. `"EST5EDT,M3.2.0,M11.1.0"` ([list](https://github.com/nayarsystems/posix_tz_db/blob/master/zones.csv)) |
| `ENABLE_SCREEN_*` | `1` | Show/hide tabs: `CLOCK`, `PCSTATS`, `PROCS`, `SYSINFO`, `QUOTES`, `YOUTUBE` |
| `AUTO_NEXT_MS` | `10000` | Auto-rotate tabs after this idle time (`0` disables) |
| `DISPLAY_ROTATION` | `1` | `1` = USB on the right, `3` = USB on the left |
| `DISPLAY_INVERT` | `true` | Flip if colors look inverted on your board |
| `BACKLIGHT_LEVEL` | `255` | Brightness, 0–255 |
| `*_POLL_MS` | various | Poll interval of each agent endpoint |
| `TOUCH_MIN/MAX_X/Y`, `TOUCH_DEBUG` | — | Touch calibration; `TOUCH_DEBUG 1` logs raw readings over serial |

### Agent — `agent/config.json`

Every key is optional; missing keys use the defaults in `DEFAULT_CONFIG` inside the script. This file is git-ignored.

```jsonc
{
  "port": 8377,
  "bind": "0.0.0.0",
  "disk_path": null,              // partition shown on the PC tab (null = system root)
  "top_procs": 4,
  "ping": { "host": "8.8.8.8", "port": 53 },

  "quotes": {
    "enabled": true,
    "base": "USD",                // currency prices are quoted in
    "prefix": "$",                // shown before prices (ASCII only, e.g. "R$", "EUR ")
    "assets": [                   // panel shows the first 4, in this order
      { "symbol": "EUR", "type": "fiat" },                      // AwesomeAPI pair EUR-USD
      { "symbol": "BTC", "type": "crypto", "id": "bitcoin" }    // CoinGecko coin id
    ]
  },

  "youtube": {
    "api_key": "",                // or set the YOUTUBE_API_KEY environment variable
    "channel_ids": ["UC..."]
  }
}
```

Use `--config path/to/config.json` and `--port N` to override from the command line.

## Agent API

| Endpoint | Example response |
| --- | --- |
| `GET /stats` | `{"cpu": 23.5, "ram": 61.2, "disk": 54.0, "disk_read_mbps": 1.02, "disk_write_mbps": 11.88, "net_down_mbps": 4.31, "net_up_mbps": 0.87}` |
| `GET /procs` | `{"procs": [{"name": "chrome", "cpu": 12.3, "mem_mb": 1840}, ...]}` |
| `GET /sysinfo` | `{"ping_ms": 14.2, "public_ip": "203.0.113.7", "uptime_s": 86400, "net_down_mbps": 4.3, "net_up_mbps": 0.9}` |
| `GET /quotes` | `{"base": "USD", "prefix": "$", "items": [{"symbol": "BTC", "price": 83799.0, "change_pct": 1.07}, ...], "updated_at": 1791766163}` |
| `GET /youtube` | `{"configured": true, "channels": [{"title": "...", "subs": 1234, "views": 56789, "videos": 42}], "updated_at": 1791766163}` |

> The agent has no authentication — keep it on a trusted LAN (or set `"bind"` to a specific interface).

## Adding a new tab

1. Create `src/screen_mytab.h` with a class deriving from `Screen` (see [`src/screens.h`](src/screens.h)): implement `name()`, `drawIcon()`, `onEnter()` (static layout, drawn once) and `update()` (redraw only what changed).
2. Fetch data with `agent::fetch(tft, "/myendpoint", doc)` from [`src/agent_client.h`](src/agent_client.h) — it handles WiFi/agent/JSON errors and the status indicator for you.
3. Use colors and widgets from [`src/theme.h`](src/theme.h) (`drawCard`, `drawBar`, `drawRingGauge`, `drawSparkline`, `drawPctBadge`, vector icons...).
4. Register it in `main.cpp` with `manager.add(&myTab)`.

## Troubleshooting

- **Background shows white/light gray** → toggle `DISPLAY_INVERT`.
- **Touch is offset** → set `TOUCH_DEBUG 1`, tap the corners, and adjust `TOUCH_MIN/MAX_X/Y`.
- **"agent offline"** → open `http://<AGENT_HOST>:8377/stats` from another device on the LAN; usually it's the firewall.
- **Windows: agent changes don't show up** → stale Python processes may be sharing port 8377. Run `stop_agent.bat`, then start again.

## License

[MIT](LICENSE)
