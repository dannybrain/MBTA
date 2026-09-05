from datetime import datetime, timezone

import config
from mbta_api import api_get, sanitize_error
from settings import settings
from ssl_certs import get_requests_verify


def fetch_predictions():
    params = {
        "filter[stop]": settings.stop_id,
        "filter[route]": settings.route,
        "sort": "arrival_time",
        "page[limit]": 20,
        "include": "trip",
    }

    verify, verify_source = get_requests_verify()
    payload = api_get("/predictions", params)
    included = {
        (item["type"], item["id"]): item for item in payload.get("included", [])
    }

    query_params = {**params, "api_key": config.API_KEY}
    url = f"{config.BASE_URL}/predictions"
    return payload["data"], included, url, query_params, verify, verify_source


def _trip_headsign(item, included):
    trip_ref = item["relationships"]["trip"]["data"]
    trip = included.get((trip_ref["type"], trip_ref["id"]), {})
    return trip.get("attributes", {}).get("headsign", "?")


def _stop_id(item):
    return item["relationships"]["stop"]["data"]["id"]


def parse_predictions(data, included, direction_id=None, now=None):
    if now is None:
        now = datetime.now(timezone.utc)

    trains = []

    for item in data:
        attr = item["attributes"]

        if attr["arrival_time"] is None:
            continue

        arrival = datetime.fromisoformat(
            attr["arrival_time"].replace("Z", "+00:00")
        )

        mins = round((arrival - now).total_seconds() / 60)

        if mins < 0:
            continue

        record = {
            "minutes": mins,
            "time": arrival.strftime("%I:%M %p").lstrip("0"),
            "direction_id": attr["direction_id"],
            "headsign": _trip_headsign(item, included),
            "stop_id": _stop_id(item),
            "arrival_iso": attr["arrival_time"],
            "prediction_id": item["id"],
        }

        if direction_id is not None and attr["direction_id"] != direction_id:
            continue

        trains.append(record)

    return trains


def get_next_trains():
    data, included, _, _, _, _ = fetch_predictions()
    trains = parse_predictions(data, included, direction_id=settings.direction_id)
    return trains[:2]


def get_debug_snapshot():
    now = datetime.now(timezone.utc)

    try:
        data, included, url, params, verify, verify_source = fetch_predictions()
    except Exception as exc:
        verify, verify_source = get_requests_verify()
        return {
            "ok": False,
            "error": sanitize_error(str(exc)),
            "now": now,
            "ssl": {"verify": verify, "source": verify_source},
        }

    all_upcoming = parse_predictions(data, included, direction_id=None, now=now)
    selected = parse_predictions(
        data, included, direction_id=settings.direction_id, now=now
    )[:2]

    masked_params = {**params, "api_key": "***"}
    query = "&".join(f"{k}={v}" for k, v in masked_params.items())

    return {
        "ok": True,
        "now": now,
        "url": url,
        "query": query,
        "ssl": {"verify": verify, "source": verify_source},
        "config": {
            "stop_id": settings.stop_id,
            "stop_name": settings.stop_name,
            "route": settings.route,
            "direction_id": settings.direction_id,
            "destination": settings.destination,
            "refresh_seconds": config.REFRESH_SECONDS,
        },
        "all_upcoming": all_upcoming,
        "selected": selected,
        "raw_count": len(data),
    }
