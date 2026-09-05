import argparse
import curses
import sys
import time

import config
from debug import print_debug_screen
from mbta import get_debug_snapshot, get_next_trains
from route_catalog import (
    fetch_directions,
    fetch_stops,
    format_help_appendix,
    resolve_direction,
    resolve_stop,
)
from settings import settings
from styles import format_styles_help, make_bevel_caps, resolve_style
from ui import draw_screen


def build_parser(stops, directions, catalog_error=None):
    parser = argparse.ArgumentParser(
        description=(
            "MBTA Green Line C arrivals. "
            "Default: Summit Avenue inbound to Government Center."
        ),
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument(
        "--stop",
        metavar="STOP",
        help="stop ID (place-sumav) or name fragment (summit, englewood)",
    )
    parser.add_argument(
        "--direction",
        metavar="DIR",
        help="direction id or alias: 0/outbound/cleveland, 1/inbound/government",
    )
    parser.add_argument(
        "--style",
        metavar="STYLE",
        help="curses display skin (default: classic); see list below",
    )
    parser.add_argument(
        "--message",
        metavar="TEXT",
        help='bevel banner text for --style custom (e.g. "GO SOX")',
    )
    parser.add_argument(
        "--debug",
        action="store_true",
        help="plain-text diagnostic output instead of curses UI",
    )

    appendix_parts = []
    if catalog_error:
        appendix_parts.append(
            f"Could not load live station list from MBTA API ({catalog_error}).\n"
            "Defaults: stop=place-sumav (Summit Avenue), direction=1 (Government Center).\n"
            "Direction aliases: 0/outbound/cleveland, 1/inbound/government"
        )
    else:
        appendix_parts.append(format_help_appendix(stops, directions))

    appendix_parts.append("")
    appendix_parts.append(format_styles_help())
    parser.epilog = "\n".join(appendix_parts)

    return parser


def apply_runtime_options(args, stops, directions):
    if args.stop:
        stop = resolve_stop(args.stop, stops)
        settings.stop_id = stop["id"]
        settings.stop_name = stop["name"]
    else:
        default_stop = resolve_stop(config.STOP_ID, stops)
        settings.stop_id = default_stop["id"]
        settings.stop_name = default_stop["name"]

    direction_id, destination = resolve_direction(args.direction, directions)
    settings.direction_id = direction_id
    settings.destination = destination
    settings.style = resolve_style(args.style)

    if settings.style == "custom":
        if not args.message:
            raise ValueError('--message is required when --style custom (e.g. --message "GO SOX")')
        settings.custom_message = args.message
        settings.cap_banner = make_bevel_caps(args.message)


def run_curses(stdscr):
    while True:
        try:
            trains = get_next_trains()
        except Exception:
            trains = []

        draw_screen(stdscr, trains)
        time.sleep(config.REFRESH_SECONDS)


def run_debug():
    while True:
        snapshot = get_debug_snapshot()
        print_debug_screen(snapshot)
        time.sleep(config.REFRESH_SECONDS)


def main():
    try:
        stops = fetch_stops()
        directions = fetch_directions()
        catalog_error = None
    except Exception as exc:
        stops, directions = [], []
        catalog_error = exc

    parser = build_parser(stops, directions, catalog_error)
    args = parser.parse_args()

    if catalog_error:
        print(f"Error: {catalog_error}", file=sys.stderr)
        sys.exit(1)

    try:
        apply_runtime_options(args, stops, directions)
    except ValueError as exc:
        print(f"Error: {exc}", file=sys.stderr)
        sys.exit(2)

    if args.debug:
        run_debug()
    else:
        curses.wrapper(run_curses)


if __name__ == "__main__":
    main()
