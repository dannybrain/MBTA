from datetime import datetime


def print_debug_screen(snapshot):
    print("\033[2J\033[H", end="")

    now_local = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    print("MBTA debug mode")
    print("=" * 72)
    print(f"Local time: {now_local}")
    print("Press Ctrl+C to exit\n")

    if not snapshot["ok"]:
        print(f"API error: {snapshot['error']}")
        if "ssl" in snapshot:
            print(f"SSL bundle: {snapshot['ssl']['verify']}")
            print(f"SSL source: {snapshot['ssl']['source']}")
        return

    cfg = snapshot["config"]
    print("Config")
    print(f"  stop          = {cfg['stop_name']} ({cfg['stop_id']})")
    print(f"  route         = {cfg['route']}")
    print(f"  direction_id  = {cfg['direction_id']}  (-> {cfg['destination']})")
    print(f"  refresh       = {cfg['refresh_seconds']}s")
    print()
    print("SSL")
    print(f"  bundle        = {snapshot['ssl']['verify']}")
    print(f"  source        = {snapshot['ssl']['source']}")
    print()
    print("Request")
    print(f"  GET {snapshot['url']}?{snapshot['query']}")
    print(f"  raw predictions returned: {snapshot['raw_count']}")
    print()

    print("All upcoming predictions (both directions)")
    print("-" * 72)
    print(
        f"{'sel':>3}  {'dir':>3}  {'min':>4}  {'arrival':>9}  "
        f"{'stop':>6}  {'headsign'}"
    )
    print("-" * 72)

    selected_ids = {t["prediction_id"] for t in snapshot["selected"]}

    if not snapshot["all_upcoming"]:
        print("  (none)")
    else:
        for train in snapshot["all_upcoming"]:
            marker = "*" if train["prediction_id"] in selected_ids else " "
            print(
                f"{marker:>3}  {train['direction_id']:>3}  {train['minutes']:>4}  "
                f"{train['time']:>9}  {train['stop_id']:>6}  {train['headsign']}"
            )

    print()
    print("Display output (filtered trains shown on normal screen)")
    print("-" * 72)

    if not snapshot["selected"]:
        print("  No upcoming trains for selected direction")
    else:
        for index, train in enumerate(snapshot["selected"], start=1):
            label = "NEXT" if index == 1 else "THEN"
            print(
                f"  {label}: {train['minutes']} min  ({train['time']})  "
                f"dir={train['direction_id']}  {train['headsign']}"
            )

    print()
    print(
        "Legend: * = included in display; dir 0 = Cleveland Circle, "
        "dir 1 = Government Center"
    )
