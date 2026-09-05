"""Inbound service window for Summit Avenue Green-C (Mac preview + firmware parity)."""

from __future__ import annotations

from dataclasses import dataclass
from datetime import date, datetime, timedelta, timezone

import config
from mbta_api import api_get

LOOKBACK_SEC = 3600
LOOKAHEAD_SEC = 48 * 3600
INBOUND_DIRECTION_ID = 1


@dataclass
class ServiceWindow:
    valid: bool = False
    overnight_active: bool = False
    service_until: datetime | None = None
    resume: datetime | None = None
    error: str | None = None


def _parse_iso(ts: str) -> datetime:
    return datetime.fromisoformat(ts.replace("Z", "+00:00"))


def _format_clock(when: datetime) -> str:
    return when.astimezone().strftime("%I:%M %p").lstrip("0")


def _resume_date(service_until: datetime) -> date:
    local = service_until.astimezone()
    if local.hour < 4:
        return local.date()
    return local.date() + timedelta(days=1)


def _fetch_first_inbound_on_date(day: date) -> datetime | None:
    payload = api_get(
        "/schedules",
        {
            "filter[stop]": config.STOP_ID,
            "filter[route]": config.ROUTE,
            "filter[direction_id]": INBOUND_DIRECTION_ID,
            "filter[date]": day.isoformat(),
            "sort": "arrival_time",
            "page[limit]": 1,
        },
    )
    data = payload.get("data", [])
    if not data:
        return None
    stamp = data[0]["attributes"].get("arrival_time") or data[0]["attributes"].get(
        "departure_time"
    )
    return _parse_iso(stamp) if stamp else None


def fetch_service_window(now: datetime | None = None) -> ServiceWindow:
    now = now or datetime.now(timezone.utc)
    window = ServiceWindow()

    try:
        payload = api_get(
            "/schedules",
            {
                "filter[stop]": config.STOP_ID,
                "filter[route]": config.ROUTE,
                "filter[direction_id]": INBOUND_DIRECTION_ID,
                "sort": "arrival_time",
                "page[limit]": 500,
            },
        )
    except Exception as exc:
        window.error = str(exc)
        return window

    epochs: list[datetime] = []
    for item in payload.get("data", []):
        stamp = item["attributes"].get("arrival_time") or item["attributes"].get("departure_time")
        if not stamp:
            continue
        when = _parse_iso(stamp)
        if when < now - timedelta(seconds=LOOKBACK_SEC):
            continue
        if when > now + timedelta(seconds=LOOKAHEAD_SEC):
            continue
        epochs.append(when)

    if not epochs:
        window.error = "Not enough schedule"
        return window

    epochs.sort()
    service_until = epochs[-1]

    resume_day = _resume_date(service_until)
    resume = _fetch_first_inbound_on_date(resume_day)
    if resume is None:
        window.error = "No resume train"
        return window

    window.valid = True
    window.service_until = service_until
    window.resume = resume
    window.overnight_active = service_until <= now < resume
    return window


def inbound_last_train_line(window: ServiceWindow) -> str | None:
    if not window.valid or not window.service_until:
        return None
    return f"Last inbound train: {_format_clock(window.service_until)}"


def night_mode_line(window: ServiceWindow) -> str | None:
    if not window.valid or not window.service_until:
        return None
    return f"Night mode starts: {_format_clock(window.service_until)}"


def resume_short_line(window: ServiceWindow) -> str | None:
    if not window.valid or not window.resume:
        return None
    return f"Live again at: {_format_clock(window.resume)}"


def service_until_footer(window: ServiceWindow, now: datetime | None = None) -> str | None:
    now = now or datetime.now(timezone.utc)
    if not window.valid or not window.service_until or now >= window.service_until:
        return None
    return f"T services until {_format_clock(window.service_until)}"
