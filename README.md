# MBTA

Real-time **MBTA Green Line C** arrivals for Summit Avenue → Government Center (inbound).

This repo has three faces that share the same route defaults in `config.py`:

1. **Terminal app** — curses display with multiple skins (original project)
2. **E-ink firmware** — runs on the [LilyGO T5 E-Paper S3](https://www.lilygo.cc/products/t5-4-7-inch-e-paper-v2-3) (540×960 portrait)
3. **Mac preview** — pygame simulator matching the e-ink layout so you can iterate without reflashing

Data comes from the [MBTA v3 API](https://www.mbta.com/developers/v3-api). The Mac preview refreshes every 30 seconds. Firmware **v4.0** maximizes battery life with deep sleep — shows static screen with battery gauge and "PRESS RST" prompt; pressing **RST** boots the device, fetches trains, displays for 90 seconds, then returns to deep sleep.

```
┌─────────────────────────────────────┐
│              GREEN C                │
├─────────────────────────────────────┤
│ Summit Avenue -> Government Center  │
├──────────────────┬──────────────────┤
│       NEXT       │       THEN       │
│        6         │       15         │
│       LIVE       │       EST        │
│     minutes      │     minutes      │
│     8:35 PM      │     8:45 PM      │
├──────────────────┴──────────────────┤
│ Weather:    (icon) 27C  72%    UV4  │
├─────────────────────────────────────┤
│ Battery:    [████░░]           85%  │
├─────────────────────────────────────┤
│ WiFi:       connected to Network    │
├─────────────────────────────────────┤
│         Updated 8:31 PM             │
└─────────────────────────────────────┘
```

## Requirements

- Python 3.12+ (terminal app and Mac preview)
- An MBTA API key ([register at MBTA](https://www.mbta.com/subscribe-to-api-features))
- macOS terminal with `curses` (stdlib) for the terminal app
- LilyGO T5 E-Paper S3 board for the firmware

Python packages: see `requirements.txt` (`requests`, `pyfiglet`, `pygame`).

## Terminal app

### Setup

```bash
git clone https://github.com/dannybrain/MBTA.git
cd MBTA
python3 -m venv ~/.venv/3.12
~/.venv/3.12/bin/pip install -r requirements.txt
```

Set your MBTA API key in the environment:

```bash
export MBTA_API_KEY="your-key-here"
```

Or assign `API_KEY` in `config.py` for local use (do not commit real keys).

### Usage

```bash
./launch.sh
```

On networks without SSL inspection you can run `python3 main.py` directly.

| Flag | Description |
|------|-------------|
| `--stop STOP` | Stop ID (`place-sumav`) or name fragment (`summit`, `kenmore`) |
| `--direction DIR` | `0` / `outbound` / `cleveland` or `1` / `inbound` / `government` |
| `--style STYLE` | Curses display skin (default: `classic`); see below |
| `--message TEXT` | Custom bevel banner when `--style custom` (max 24 chars) |
| `--debug` | Plain-text diagnostics instead of the curses UI |
| `--help` | Usage plus live list of all Green Line C stations and directions |

Display styles (`--style`): `classic`, `double`, `minimal`, `kindle`, `greenline`, `ticker`, `boston`, `mbta`, `custom`.

## E-ink firmware (v4.0)

Firmware for the LilyGO T5 E-Paper S3 lives in `firmware/`. **v4.0** maximizes battery life with deep sleep mode — displays static screen with battery gauge, "PRESS RST to check trains", and last-checked time. Press **RST** to wake the device, fetch real-time train data, and display for 90 seconds before returning to deep sleep.

Copy `firmware/include/secrets.h.example` to `firmware/include/secrets.h` and fill in WiFi credentials plus your MBTA API key. That file is gitignored.

The firmware uses M5GFX with the board's PCA9535 I/O expander and TPS65185 EPD power driver (`lilygo_t5_display.h`), reads battery state from the onboard BQ27220 fuel gauge, and uses deep sleep for maximum power efficiency.

**v4.0:** Complete power optimization — removed touch polling, added deep sleep mode, simplified to RST-button-only operation. Static screen shows battery gauge with label, "PRESS RST to check trains", and prominent "Last checked" time. Battery life extended from days to weeks/months.

**v3.0:** Standby home screen with touch wake; tap on standby shows **Loading…** while WiFi connects. Quote advances on each wake; last-checked time persisted in NVS. Active session lasts **90 s**, then returns to standby.

**v2.3:** Tap toggled an **info board**; overnight **service paused** screen with no polling until resume.

**v2.2:** THEN panel showed a second stacked train row (older layout).

**v2.1:** Option B predictions matching the Mac preview; continuous WiFi refresh loop (superseded by v3.0 standby).

Hardware target: **LilyGO T5 E-Paper S3**, portrait 540×960. Layout constants and the editable visual spec are in `epaper_layout.py` and `DISPLAY_DESIGN.md`.

**USB / flashing:** The board uses native USB (`/dev/cu.usbmodem*`). In v4.0, the device enters deep sleep after the active session, so the USB port will disappear. Press **RST** to wake the device for flashing, or hold **BOOT** + **RST** and flash immediately.

## Mac preview

Run the pygame simulator to see the e-ink layout on your Mac before flashing:

```bash
./preview.sh              # save preview.png and open in Preview
./preview.sh --live       # live window, refreshes every 30 s
./preview.sh --battery 42 --charging
./preview.sh --wifi "MyNetwork"
./preview.sh --standby       # v4.0 static screen with battery gauge
./preview.sh --loading       # loading screen during fetch
```

Uses `~/.venv/3.12/bin/python3` by default (same venv as the terminal app).

Shared layout constants live in `epaper_layout.py`. Train arrivals use **Option B** inference in `epaper_predictions.py` — **NEXT** and **THEN** as two equal panels with LIVE / EST / SCHED labels. Weather data (Open-Meteo, Brookline coordinates) is in `epaper_weather.py`. Layout details are in `DISPLAY_DESIGN.md`.

## Direction reference (Green Line C)

| `direction_id` | Name | Destination |
|----------------|------|-------------|
| `0` | West / outbound | Cleveland Circle |
| `1` | East / inbound | Government Center |

## License

[The Unlicense](LICENSE) — public domain. Use, modify, and share freely with no obligations.
