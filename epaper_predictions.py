"""Summit Avenue arrival estimates (Option B: platform time + LIVE / EST / SCHED).

NEXT prefers live MBTA predictions at Summit; falls back to Cleveland departure
+ scheduled travel time (EST) or timetable (SCHED). THEN shows the next distinct
trip after NEXT (usually EST or SCHED).

Display refinement:
- NEXT is always the soonest LIVE train when any LIVE exists; never an EST/SCHED minute count.
- THEN prefers the next LIVE after NEXT; EST/SCHED only when no second LIVE is available.
- Drop EST/SCHED sandwiched immediately before a later LIVE (blended-source artifact).
- When a LIVE NEXT expires locally, only promote THEN if it is also LIVE (never promote EST to NEXT).
"""

from __future__ import annotations

from dataclasses import dataclass
from datetime import datetime, timedelta, timezone
from enum import Enum

import config
from mbta_api import api_get

SUMMIT_STOP = "place-sumav"
CLEVELAND_STOP = "place-clmnl"
DEFAULT_TRAVEL_MINUTES = 8
DISPLAY_TRAIN_COUNT = 2  # NEXT + THEN

_SOURCE_RANK = {"LIVE": 0, "EST": 1, "SCHED": 2}


class PredictionSource(str, Enum):
    LIVE = "LIVE"
    EST = "EST"
    SCHED = "SCHED"


@dataclass(frozen=True)
class ArrivalEstimate:
    minutes: int
    clock_time: str
    source: PredictionSource
    trip_id: str | None = None


def _parse_iso(ts: str) -> datetime:
    return datetime.fromisoformat(ts.replace("Z", "+00:00"))


def _clock_text(when: datetime) -> str:
    return when.astimezone().strftime("%I:%M %p").lstrip("0")


def _minutes_until(when: datetime, now: datetime) -> int:
    return round((when - now).total_seconds() / 60)


def scheduled_travel_minutes(
    route: str = config.ROUTE,
    direction_id: int = config.DIRECTION_ID,
) -> int:
    """Median scheduled Cleveland Circle → Summit travel time from the timetable."""
    payload = api_get(
        "/schedules",
        {
            "filter[route]": route,
            "filter[direction_id]": direction_id,
            "sort": "departure_time",
            "page[limit]": 500,
        },
    )
    by_trip: dict[str, dict[str, datetime]] = {}
    for item in payload.get("data", []):
        stop_id = item["relationships"]["stop"]["data"]["id"]
        if stop_id not in (CLEVELAND_STOP, SUMMIT_STOP):
            continue
        stamp = item["attributes"].get("departure_time") or item["attributes"].get("arrival_time")
        if not stamp:
            continue
        trip_id = item["relationships"]["trip"]["data"]["id"]
        by_trip.setdefault(trip_id, {})[stop_id] = _parse_iso(stamp)

    deltas = [
        round((stops[SUMMIT_STOP] - stops[CLEVELAND_STOP]).total_seconds() / 60)
        for stops in by_trip.values()
        if CLEVELAND_STOP in stops and SUMMIT_STOP in stops
    ]
    if not deltas:
        return DEFAULT_TRAVEL_MINUTES
    deltas.sort()
    return deltas[len(deltas) // 2]


def _fetch_predictions(stop_id: str, route: str, direction_id: int) -> tuple[list, dict]:
    payload = api_get(
        "/predictions",
        {
            "filter[stop]": stop_id,
            "filter[route]": route,
            "filter[direction_id]": direction_id,
            "sort": "arrival_time",
            "page[limit]": 20,
            "include": "trip,vehicle",
        },
    )
    included = {(i["type"], i["id"]): i for i in payload.get("included", [])}
    return payload.get("data", []), included


def _fetch_scheduled_stop(
    stop_id: str,
    route: str,
    direction_id: int,
    source: PredictionSource,
    travel_minutes: int = 0,
    now: datetime | None = None,
) -> list[ArrivalEstimate]:
    if now is None:
        now = datetime.now(timezone.utc)
    payload = api_get(
        "/schedules",
        {
            "filter[stop]": stop_id,
            "filter[route]": route,
            "filter[direction_id]": direction_id,
            "sort": "arrival_time" if stop_id == SUMMIT_STOP else "departure_time",
            "page[limit]": 500,
            "include": "trip",
        },
    )
    rows: list[ArrivalEstimate] = []
    for item in payload.get("data", []):
        stamp = item["attributes"].get("arrival_time") or item["attributes"].get("departure_time")
        if not stamp:
            continue
        when = _parse_iso(stamp)
        if travel_minutes:
            when += timedelta(minutes=travel_minutes)
        mins = _minutes_until(when, now)
        if mins < 0:
            continue
        trip_id = item["relationships"]["trip"]["data"]["id"]
        rows.append(ArrivalEstimate(mins, _clock_text(when), source, trip_id))
    rows.sort(key=lambda e: (e.minutes, _SOURCE_RANK[e.source.value]))
    return rows


def _merge_candidates(items: list[ArrivalEstimate]) -> list[ArrivalEstimate]:
    by_trip: dict[str, ArrivalEstimate] = {}
    extras: list[ArrivalEstimate] = []
    for estimate in items:
        if estimate.trip_id:
            existing = by_trip.get(estimate.trip_id)
            if existing is None or _SOURCE_RANK[estimate.source.value] < _SOURCE_RANK[existing.source.value]:
                by_trip[estimate.trip_id] = estimate
        else:
            extras.append(estimate)
    return sorted(
        list(by_trip.values()) + extras,
        key=lambda e: (e.minutes, _SOURCE_RANK[e.source.value]),
    )


def _pick_distinct(
    merged: list[ArrivalEstimate],
    pool: list[ArrivalEstimate],
    target: int,
) -> list[ArrivalEstimate]:
    if not merged:
        return []
    live = [estimate for estimate in merged if estimate.source == PredictionSource.LIVE]
    result = [live[0] if live else merged[0]]
    seen = {result[0].trip_id}
    after_minutes = result[0].minutes
    while len(result) < target:
        next_train = _next_train_after(merged, pool, seen, after_minutes)
        if next_train is None:
            break
        result.append(next_train)
        seen.add(next_train.trip_id)
        after_minutes = next_train.minutes
    return result


def _is_estimate(source: PredictionSource) -> bool:
    return source in (PredictionSource.EST, PredictionSource.SCHED)


def _next_train_after(
    merged: list[ArrivalEstimate],
    pool: list[ArrivalEstimate],
    seen: set[str | None],
    after_minutes: int,
) -> ArrivalEstimate | None:
    for preferred in (PredictionSource.LIVE, PredictionSource.EST, PredictionSource.SCHED):
        for estimate in merged:
            if estimate.trip_id in seen or estimate.minutes <= after_minutes:
                continue
            if estimate.source == preferred:
                return estimate
    for estimate in pool:
        if estimate.trip_id in seen or estimate.minutes <= after_minutes:
            continue
        return estimate
    return None


def _scrub_estimates_before_live(picked: list[ArrivalEstimate]) -> list[ArrivalEstimate]:
    """Drop EST/SCHED that sit immediately ahead of a later LIVE (same artifact as sandwich)."""
    result = list(picked)
    while True:
        removed = False
        for i in range(len(result) - 1):
            current = result[i]
            following = result[i + 1]
            if (
                _is_estimate(current.source)
                and following.source == PredictionSource.LIVE
                and current.minutes <= following.minutes
            ):
                result.pop(i)
                removed = True
                break
        if not removed:
            return result


def _refill_picked_trains(
    picked: list[ArrivalEstimate],
    merged: list[ArrivalEstimate],
    pool: list[ArrivalEstimate],
    target: int = DISPLAY_TRAIN_COUNT,
) -> list[ArrivalEstimate]:
    result = list(picked)
    seen = {estimate.trip_id for estimate in result}
    after_minutes = result[-1].minutes if result else -1

    while len(result) < target:
        next_train = _next_train_after(merged, pool, seen, after_minutes)
        if next_train is None:
            break
        result.append(next_train)
        seen.add(next_train.trip_id)
        after_minutes = next_train.minutes
    return result


def _refine_picked_trains(
    picked: list[ArrivalEstimate],
    merged: list[ArrivalEstimate],
    pool: list[ArrivalEstimate],
) -> list[ArrivalEstimate]:
    picked = _scrub_estimates_before_live(picked)
    return _refill_picked_trains(picked, merged, pool)


def get_summit_arrivals(
    route: str = config.ROUTE,
    direction_id: int = config.DIRECTION_ID,
    now: datetime | None = None,
) -> list[ArrivalEstimate]:
    if now is None:
        now = datetime.now(timezone.utc)

    travel_min = scheduled_travel_minutes(route, direction_id)
    candidates: list[ArrivalEstimate] = []

    summit_data, _ = _fetch_predictions(SUMMIT_STOP, route, direction_id)
    for item in summit_data:
        attr = item["attributes"]
        stamp = attr.get("arrival_time") or attr.get("departure_time")
        if not stamp:
            continue
        when = _parse_iso(stamp)
        mins = _minutes_until(when, now)
        if mins < 0:
            continue
        trip_id = item["relationships"]["trip"]["data"]["id"]
        has_vehicle = item["relationships"].get("vehicle", {}).get("data") is not None
        source = PredictionSource.LIVE if has_vehicle else PredictionSource.SCHED
        candidates.append(ArrivalEstimate(mins, _clock_text(when), source, trip_id))

    cleveland_data, _ = _fetch_predictions(CLEVELAND_STOP, route, direction_id)
    for item in cleveland_data:
        attr = item["attributes"]
        stamp = attr.get("departure_time") or attr.get("arrival_time")
        if not stamp:
            continue
        when = _parse_iso(stamp) + timedelta(minutes=travel_min)
        mins = _minutes_until(when, now)
        if mins < 0:
            continue
        trip_id = item["relationships"]["trip"]["data"]["id"]
        has_vehicle = item["relationships"].get("vehicle", {}).get("data") is not None
        source = PredictionSource.LIVE if has_vehicle else PredictionSource.EST
        candidates.append(ArrivalEstimate(mins, _clock_text(when), source, trip_id))

    candidates.extend(
        _fetch_scheduled_stop(SUMMIT_STOP, route, direction_id, PredictionSource.SCHED, now=now)
    )
    candidates.extend(
        _fetch_scheduled_stop(
            CLEVELAND_STOP,
            route,
            direction_id,
            PredictionSource.EST,
            travel_minutes=travel_min,
            now=now,
        )
    )

    merged = _merge_candidates(candidates)
    if not merged:
        return []

    picked = _pick_distinct(merged, candidates, DISPLAY_TRAIN_COUNT)
    if not picked:
        return []

    return _refine_picked_trains(picked, merged, candidates)
