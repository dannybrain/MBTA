"""Open-Meteo weather helpers for Brookline, MA (e-paper display)."""

from __future__ import annotations

from dataclasses import dataclass
from datetime import datetime, timedelta

import requests

from ssl_certs import get_requests_verify

BROOKLINE_LAT = 42.3318
BROOKLINE_LON = -71.1212
OPEN_METEO_URL = "https://api.open-meteo.com/v1/forecast"


@dataclass(frozen=True)
class WeatherSnapshot:
    weather_code: int
    icon: str
    label: str
    precip_percent: int
    humidity_percent: int
    temp_c: int
    uv_index: int

    def values_text(self) -> str:
        return f"{self.temp_c}C  {self.humidity_percent}%  UV{self.uv_index}"


def weather_icon(code: int) -> tuple[str, str]:
    if code == 0:
        return "\u2600", "Clear"
    if code in (1, 2):
        return "\u26c5", "Partly cloudy"
    if code == 3:
        return "\u2601", "Cloudy"
    if code in (45, 48):
        return "\u2594", "Fog"
    if code in (51, 53, 55, 56, 57, 61, 63, 65, 66, 67, 80, 81, 82):
        return "\u2614", "Rain"
    if code in (71, 73, 75, 77, 85, 86):
        return "\u2744", "Snow"
    if code in (95, 96, 99):
        return "\u26a1", "Storm"
    return "\u2753", "Unknown"


@dataclass(frozen=True)
class TomorrowForecast:
    day_label: str
    weather_code: int
    icon: str
    temp_high_c: int
    temp_low_c: int
    humidity_percent: int
    uv_index: int
    wind_kmh: int
    rain_note: str


def fetch_tomorrow_forecast() -> TomorrowForecast | None:
    params = {
        "latitude": BROOKLINE_LAT,
        "longitude": BROOKLINE_LON,
        "daily": (
            "weather_code,temperature_2m_max,temperature_2m_min,uv_index_max,"
            "precipitation_probability_max,wind_speed_10m_max"
        ),
        "hourly": "relative_humidity_2m,precipitation_probability",
        "timezone": "America/New_York",
        "forecast_days": 2,
    }
    verify, _ = get_requests_verify()
    response = requests.get(OPEN_METEO_URL, params=params, timeout=10, verify=verify)
    response.raise_for_status()
    payload = response.json()

    daily = payload.get("daily", {})
    codes = daily.get("weather_code", [])
    highs = daily.get("temperature_2m_max", [])
    lows = daily.get("temperature_2m_min", [])
    uvs = daily.get("uv_index_max", [])
    precips = daily.get("precipitation_probability_max", [])
    winds = daily.get("wind_speed_10m_max", [])
    if len(codes) < 2 or len(highs) < 2 or len(lows) < 2:
        return None

    code = int(codes[1])
    icon, _ = weather_icon(code)
    tomorrow = datetime.now().astimezone().date() + timedelta(days=1)
    day_label = tomorrow.strftime("%a %b %d")

    humidity = -1
    rain_note = "None expected"
    hourly = payload.get("hourly", {})
    times = hourly.get("time", [])
    humidities = hourly.get("relative_humidity_2m", [])
    probs = hourly.get("precipitation_probability", [])
    tomorrow_key = tomorrow.isoformat()
    humidity_sum = 0
    humidity_count = 0
    rain_start = -1
    rain_end = -1
    for idx, stamp in enumerate(times):
        if not stamp.startswith(tomorrow_key):
            continue
        hour = int(stamp[11:13]) if len(stamp) >= 13 else -1
        if hour < 6 or hour > 22:
            continue
        if idx < len(humidities):
            humidity_sum += int(humidities[idx])
            humidity_count += 1
        if idx < len(probs) and int(probs[idx]) >= 40:
            if rain_start < 0:
                rain_start = hour
            rain_end = hour
    if humidity_count:
        humidity = humidity_sum // humidity_count
    precip = int(precips[1]) if len(precips) > 1 else 0
    if code in (51, 53, 55, 56, 57, 61, 63, 65, 66, 67, 80, 81, 82, 95, 96, 99) or precip >= 40 or rain_start >= 0:
        if rain_start >= 0 and rain_end >= rain_start:
            start_h = rain_start % 12 or 12
            end_h = (rain_end + 1) % 12 or 12
            start_ampm = "AM" if rain_start < 12 else "PM"
            end_ampm = "AM" if (rain_end + 1) % 24 < 12 else "PM"
            rain_note = f"Rain likely {start_h} {start_ampm}-{end_h} {end_ampm}"
        else:
            rain_note = f"Rain likely ({precip}%)"

    return TomorrowForecast(
        day_label=day_label,
        weather_code=code,
        icon=icon,
        temp_high_c=int(round(float(highs[1]))),
        temp_low_c=int(round(float(lows[1]))),
        humidity_percent=humidity,
        uv_index=int(round(float(uvs[1]))) if len(uvs) > 1 else -1,
        wind_kmh=int(round(float(winds[1]))) if len(winds) > 1 else -1,
        rain_note=rain_note,
    )


def fetch_brookline_weather() -> WeatherSnapshot | None:
    params = {
        "latitude": BROOKLINE_LAT,
        "longitude": BROOKLINE_LON,
        "current": "weather_code,relative_humidity_2m,temperature_2m,uv_index",
        "hourly": "precipitation_probability",
        "timezone": "America/New_York",
        "forecast_hours": 24,
    }
    verify, _ = get_requests_verify()
    response = requests.get(OPEN_METEO_URL, params=params, timeout=10, verify=verify)
    response.raise_for_status()
    payload = response.json()

    current = payload.get("current", {})
    code = int(current.get("weather_code", -1))
    humidity = int(current.get("relative_humidity_2m", -1))
    temp = int(round(float(current.get("temperature_2m", -999))))
    uv = int(round(float(current.get("uv_index", -1))))
    if code < 0 or humidity < 0 or temp <= -100 or uv < 0:
        return None

    precip = 0
    hourly = payload.get("hourly", {})
    times = hourly.get("time", [])
    probs = hourly.get("precipitation_probability", [])
    if times and probs:
        now_key = datetime.now().astimezone().strftime("%Y-%m-%dT%H:00")
        for idx, stamp in enumerate(times):
            if stamp == now_key or stamp.startswith(now_key[:13]):
                precip = int(probs[idx])
                break

    icon, label = weather_icon(code)
    return WeatherSnapshot(
        weather_code=code,
        icon=icon,
        label=label,
        precip_percent=precip,
        humidity_percent=humidity,
        temp_c=temp,
        uv_index=uv,
    )
