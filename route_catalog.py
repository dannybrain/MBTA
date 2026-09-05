import config
from mbta_api import api_get


def fetch_directions(route=None):
    route = route or config.ROUTE
    payload = api_get(f"/routes/{route}")
    attrs = payload["data"]["attributes"]
    names = attrs.get("direction_names") or []
    destinations = attrs.get("direction_destinations") or []

    directions = []
    for direction_id, destination in enumerate(destinations):
        label = names[direction_id] if direction_id < len(names) else ""
        directions.append(
            {
                "direction_id": direction_id,
                "label": label,
                "destination": destination,
            }
        )
    return directions


def fetch_stops(route=None):
    route = route or config.ROUTE
    stops = []
    offset = 0

    while True:
        payload = api_get(
            "/stops",
            {
                "filter[route]": route,
                "page[limit]": 100,
                "page[offset]": offset,
            },
        )
        for item in payload["data"]:
            attrs = item["attributes"]
            if attrs.get("location_type") != 1:
                continue
            stops.append(
                {
                    "id": item["id"],
                    "name": attrs.get("name") or item["id"],
                    "municipality": attrs.get("municipality") or "",
                }
            )

        next_link = payload.get("links", {}).get("next")
        if not next_link:
            break
        offset += 100

    return sorted(stops, key=lambda stop: stop["name"].lower())


def destination_for_direction(directions, direction_id):
    for direction in directions:
        if direction["direction_id"] == direction_id:
            return direction["destination"]
    return config.DESTINATION


def resolve_stop(query, stops):
    if not query:
        return None

    normalized = query.strip().lower()

    for stop in stops:
        if stop["id"].lower() == normalized:
            return stop

    exact_name = [stop for stop in stops if stop["name"].lower() == normalized]
    if len(exact_name) == 1:
        return exact_name[0]

    partial = [
        stop
        for stop in stops
        if normalized in stop["name"].lower()
        or normalized in stop["id"].lower().replace("place-", "")
    ]
    if len(partial) == 1:
        return partial[0]
    if len(partial) > 1:
        names = ", ".join(stop["name"] for stop in partial[:5])
        raise ValueError(f"Stop {query!r} is ambiguous. Matches include: {names}")

    raise ValueError(f"Unknown stop {query!r}. Use --help to list Green Line C stations.")


def resolve_direction(query, directions):
    if query is None:
        return config.DIRECTION_ID, config.DESTINATION

    normalized = str(query).strip().lower()
    aliases = {
        "0": 0,
        "1": 1,
        "outbound": 0,
        "inbound": 1,
        "west": 0,
        "east": 1,
        "cleveland": 0,
        "cleveland-circle": 0,
        "government": 1,
        "government-center": 1,
        "govcenter": 1,
    }

    if normalized in aliases:
        direction_id = aliases[normalized]
        return direction_id, destination_for_direction(directions, direction_id)

    if normalized.isdigit():
        direction_id = int(normalized)
        for direction in directions:
            if direction["direction_id"] == direction_id:
                return direction_id, direction["destination"]
        raise ValueError(f"Unknown direction id {direction_id}.")

    for direction in directions:
        destination = direction["destination"].lower()
        if normalized in destination or normalized.replace("-", " ") in destination:
            return direction["direction_id"], direction["destination"]

    valid = ", ".join(
        f"{d['direction_id']} ({d['destination']})" for d in directions
    )
    raise ValueError(f"Unknown direction {query!r}. Valid values: {valid}")


def format_help_appendix(stops, directions):
    lines = [
        "Green Line C stations:",
    ]
    for stop in stops:
        municipality = f" ({stop['municipality']})" if stop["municipality"] else ""
        lines.append(f"  {stop['id']:<14} {stop['name']}{municipality}")

    lines.append("")
    lines.append("Directions:")
    for direction in directions:
        default = "  [default]" if direction["direction_id"] == config.DIRECTION_ID else ""
        label = direction["label"]
        if label:
            lines.append(
                f"  {direction['direction_id']}  {label:<5} -> {direction['destination']}{default}"
            )
        else:
            lines.append(
                f"  {direction['direction_id']}  -> {direction['destination']}{default}"
            )

    lines.extend(
        [
            "",
            "Direction aliases: 0/outbound/west/cleveland, 1/inbound/east/government",
            "",
            "Examples:",
            "  ./launch.sh",
            "  ./launch.sh --stop place-sumav --direction inbound",
            "  ./launch.sh --stop summit --direction 0 --style kindle",
            "  ./launch.sh --stop summit --direction 0 --debug",
        ]
    )
    return "\n".join(lines)
