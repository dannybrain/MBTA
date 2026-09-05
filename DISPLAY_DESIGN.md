# E-Paper Display Design

> **How to read this file:** Each row is one section. Text in quotes is shown **exactly**.  
> A horizontal line separates every section.

**Canvas:** 540 × 960 px · portrait · black & white · 3 min refresh (firmware)

---

## Firmware refresh (v2.2)

| Setting | Behavior |
|---------|----------|
| Refresh interval | Fixed **3 min** — fetch MBTA + weather, redraw e-ink |
| Manual refresh | **RST** button (back) — reboots and fetches immediately |
| WiFi | Stays connected (`setAutoReconnect`, no disconnect between cycles) |
| Time sync | NTP before each MBTA fetch |
| Stale estimates | If only EST/SCHED and the first estimate passes without LIVE → “No trains” until LIVE returns |
| Overnight pause | From last inbound schedule at Summit until first morning train (~2.5 h+ gap): overnight screen, no API refresh (15 min wake to recheck clock) |
| Service footer | `Updated …` only on main screen; service hours on info board TIMETABLE |

## Touch toggle (v2.3 — Option G)

| Action | Screen |
|--------|--------|
| Tap anywhere on the display | **Info board** — alerts, Option B timetable, tomorrow weather |
| Tap again | **Normal** — NEXT / THEN board |
| Round **HOME** key | Backlight toggle (unchanged) |

**Info board layout — TOMORROW ⅔ top, TIMETABLE ⅓ bottom:**

```
┌─────────────────────────────────────┐
│            TOMORROW                 │
│         Tue Jul 21                  │
├─────────────────────────────────────┤
│           9:15 PM                   │
├─────────────────────────────────────┤
│ Weather:                            │
│ ┌────┬──────────────────────────┐   │
│ │icon│ Max:      24C             │   │
│ │    │ Min:      18C  (Font2)    │   │
│ │    │ Humidity: 65%             │   │
│ │    │ UV:       6               │   │
│ │    │ Wind:     12 km/h         │   │
│ ├────┴──────────────────────────┤   │
│ │ Rain:     None expected       │   │
│ └───────────────────────────────┘   │
├─────────────────────────────────────┤  ← split at ⅔
│           TIMETABLE                 │
│ Summit Avenue · Green-C             │
│ Last inbound train: 1:38 AM         │
│ Night mode starts: 1:38 AM          │
│ Live again at: 5:12 AM              │
├─────────────────────────────────────┤
│ Alert: … (if any)                   │
├─────────────────────────────────────┤
│  min   time   destination   src     │  only when trains exist
│  * 12m  8:45 PM  Govt Center  SCHED  │
│                                     │
│     No upcoming trains              │  centered empty state
│         Touch to return             │
└─────────────────────────────────────┘
```

Both screens share the same **3 min** refresh; a tap switches immediately.

---

## Alignment rules

| Section | Alignment |
|---------|-----------|
| Banner (`GREEN C`) | **centered** (starts below top margin; see `TOP_PAD` in `epaper_layout.py`) |
| Route line | **centered** |
| NEXT / THEN panels | unchanged |
| Weather / Battery / WiFi | label **left**, value from **screen center → right** |
| Footer (`Updated …`) | **centered** |

---

## Train predictions (Option B — preview)

Platform time only (Summit Avenue). One minutes value per panel.

| Label | Meaning | Source priority |
|-------|---------|-------------------|
| **LIVE** | MBTA real-time at Summit, vehicle linked | Highest |
| **EST** | Cleveland Circle departure + scheduled travel (~8 min) | Middle |
| **SCHED** | Timetable at Summit (or API prediction without vehicle) | Fallback |

- **NEXT** — best available: LIVE → EST → SCHED (deduped by trip).
- **THEN** — split panel with the **2nd** and **3rd** distinct trips after NEXT (usually EST or SCHED; LIVE when tracked).
- NEXT uses a large minutes value + source + `minutes` label + clock.
- Each THEN half uses a compact layout: minutes + source + clock (no `minutes` label).

Logic lives in `epaper_predictions.py` (Mac preview) and `firmware/src/predictions.cpp` (device).

---

## Layout (top → bottom)

```
┌─────────────────────────────────────┐
│            "GREEN C"                │  centered, AsciiFont8x16 × 2
├─────────────────────────────────────┤
│ "Summit Avenue -> Government Center"│  centered, Font4
├─────────────────────────────────────┤
│   NEXT              THEN            │
│    12               21              │  large minutes (NEXT only)
│   LIVE              EST             │  source labels
│  minutes           ───────          │  NEXT keeps "minutes"; THEN is split
│  7:12 PM            32              │
│                   SCHED             │
│                  7:41 PM            │
├─────────────────────────────────────┤
│ Weather:    (icon) 27C  72%    UV4  │  label left | value from center
├─────────────────────────────────────┤
│ Battery:    [████░░]           85%  │  label left | bar+value from center
├─────────────────────────────────────┤
│ WiFi:       connected               │  label left | value from center
├─────────────────────────────────────┤
│        "Updated 4:05 PM"            │  centered footer
└─────────────────────────────────────┘
```

---

## Section content

| # | Label (left) | Value (center → right) |
|---|--------------|------------------------|
| 1 | — | `GREEN C` centered |
| 2 | — | `Summit Avenue -> Government Center` centered |
| 3 | — | NEXT panel + split THEN panel (2 following trains) |
| 4 | `Weather:` | icon + temp°C + humidity% + UV index |
| 5 | `Battery:` | bar + percent (+ charging) |
| 6 | `WiFi:` | connected or offline |
| 7 | — | `Updated H:MM AM/PM` centered (no leading zero on hour) |

---

## Notes

<!-- your edits here -->
