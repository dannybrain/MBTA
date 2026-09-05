from dataclasses import dataclass
from datetime import datetime

import curses

from settings import settings


@dataclass(frozen=True)
class StyleInfo:
    style_id: str
    description: str


STYLES: dict[str, StyleInfo] = {
    "classic": StyleInfo("classic", "ASCII plus boxes"),
    "double": StyleInfo("double", "Equals-sign heavy frame"),
    "minimal": StyleInfo("minimal", "No frames, spaced columns"),
    "kindle": StyleInfo("kindle", "Horizontal rules, eInk-like spacing"),
    "greenline": StyleInfo("greenline", "Classic layout with green route accent"),
    "ticker": StyleInfo("ticker", "Compact tape-style readout"),
    "boston": StyleInfo("boston", "Bevel capitals: BOSTON"),
    "mbta": StyleInfo("mbta", "Bevel capitals: MBTA"),
    "custom": StyleInfo("custom", "Bevel capitals from --message TEXT"),
}

DEFAULT_STYLE = "classic"
BEVEL_FONT = "rectangles"
CLASSIC_BOX = {"tl": "+", "tr": "+", "bl": "+", "br": "+", "h": "-", "v": "|"}
MAX_CUSTOM_MESSAGE = 24


def make_bevel_caps(text):
    import pyfiglet

    cleaned = "".join(ch for ch in text.upper() if ch.isalnum() or ch == " ")
    cleaned = " ".join(cleaned.split())
    if not cleaned:
        raise ValueError("Message must contain at least one letter or digit.")

    if len(cleaned) > MAX_CUSTOM_MESSAGE:
        raise ValueError(
            f"Message too long for bevel display (max {MAX_CUSTOM_MESSAGE} characters)."
        )

    art = pyfiglet.figlet_format(cleaned, font=BEVEL_FONT)
    lines = [line.rstrip() for line in art.splitlines() if line.strip()]
    if not lines:
        lines = [cleaned]
    return lines


BOSTON_CAP = make_bevel_caps("BOSTON")
MBTA_CAP = make_bevel_caps("MBTA")

RENDERERS = {}


def register(style_id):
    def decorator(func):
        RENDERERS[style_id] = func
        return func

    return decorator


def list_styles():
    return list(STYLES.values())


def format_styles_help():
    lines = ["Display styles (--style, curses UI only):"]
    for info in list_styles():
        default = "  [default]" if info.style_id == DEFAULT_STYLE else ""
        lines.append(f"  {info.style_id:<10} {info.description}{default}")
    lines.append("")
    lines.append("  custom requires --message \"YOUR TEXT\" (letters/digits, max 24 chars)")
    return "\n".join(lines)


def resolve_style(name):
    if not name:
        return DEFAULT_STYLE

    normalized = name.strip().lower()
    if normalized in STYLES:
        return normalized

    matches = [sid for sid in STYLES if normalized in sid]
    if len(matches) == 1:
        return matches[0]

    valid = ", ".join(STYLES.keys())
    raise ValueError(f"Unknown style {name!r}. Valid styles: {valid}")


def get_renderer(style_id):
    return RENDERERS.get(style_id, RENDERERS[DEFAULT_STYLE])


def center_text(stdscr, y, text, attr=0):
    h, w = stdscr.getmaxyx()
    if y >= h:
        return
    x = max((w - len(text)) // 2, 0)
    try:
        stdscr.addstr(y, x, text[: max(w - x, 0)], attr)
    except curses.error:
        pass


def _train_values(trains):
    if len(trains) >= 1:
        next_minutes = str(trains[0]["minutes"])
        next_time = trains[0]["time"]
    else:
        next_minutes = "--"
        next_time = "--:--"

    if len(trains) >= 2:
        then_minutes = str(trains[1]["minutes"])
        then_time = trains[1]["time"]
    else:
        then_minutes = "--"
        then_time = "--:--"

    return next_minutes, next_time, then_minutes, then_time


def _draw_box(stdscr, y, x, width, title, minutes, arrival, chars):
    inner = width - 2
    top = chars["tl"] + chars["h"] * inner + chars["tr"]
    mid = chars["v"] + " " * inner + chars["v"]
    bot = chars["bl"] + chars["h"] * inner + chars["br"]

    stdscr.addstr(y, x, top)
    stdscr.addstr(y + 1, x, mid)
    stdscr.addstr(y + 1, x + (width - len(title)) // 2, title, curses.A_BOLD)
    stdscr.addstr(y + 2, x, mid)
    stdscr.addstr(y + 2, x + (width - len(minutes)) // 2, minutes, curses.A_BOLD)
    stdscr.addstr(y + 3, x, mid)
    stdscr.addstr(y + 3, x + (width - len("minutes")) // 2, "minutes")
    stdscr.addstr(y + 4, x, mid)
    stdscr.addstr(y + 4, x + (width - len(arrival)) // 2, arrival)
    stdscr.addstr(y + 5, x, bot)


def _draw_header(stdscr, start_y, route_attr=0):
    center_text(stdscr, start_y, "GREEN C", curses.A_BOLD | route_attr)
    center_text(stdscr, start_y + 1, settings.stop_name)
    center_text(stdscr, start_y + 2, f"→ {settings.destination}")


def _draw_footer(stdscr, h, w, sep_char="-"):
    updated = datetime.now().strftime("%H:%M:%S")
    if h > 3:
        stdscr.hline(h - 3, 0, sep_char, w)
        center_text(stdscr, h - 2, f"Updated {updated}")


def _draw_panels(stdscr, w, box_width, spacing, start_y, minutes_a, time_a, minutes_b, time_b, chars):
    total_width = box_width * 2 + spacing
    start_x = max((w - total_width) // 2, 0)
    _draw_box(stdscr, start_y, start_x, box_width, "NEXT", minutes_a, time_a, chars)
    _draw_box(
        stdscr,
        start_y,
        start_x + box_width + spacing,
        box_width,
        "THEN",
        minutes_b,
        time_b,
        chars,
    )


def _draw_banner(stdscr, start_y, lines, attr=0):
    y = start_y
    for line in lines:
        center_text(stdscr, y, line, attr)
        y += 1
    return y


def _render_cap_style(
    stdscr,
    trains,
    cap_lines,
    footer_char="-",
    cap_attr=0,
    route_attr=0,
):
    h, w = stdscr.getmaxyx()
    y = _draw_banner(stdscr, 0, cap_lines, cap_attr)
    y += 1
    _draw_header(stdscr, y, route_attr=route_attr)
    panel_y = y + 4
    n_m, n_t, t_m, t_t = _train_values(trains)
    _draw_panels(stdscr, w, 20, 4, panel_y, n_m, n_t, t_m, t_t, CLASSIC_BOX)
    _draw_footer(stdscr, h, w, footer_char)


def _init_color_pairs():
    if not curses.has_colors():
        return {}
    curses.start_color()
    pairs = {}
    try:
        curses.use_default_colors()
        curses.init_pair(1, curses.COLOR_GREEN, -1)
        pairs["green"] = curses.color_pair(1)
        curses.init_pair(2, curses.COLOR_YELLOW, -1)
        pairs["gold"] = curses.color_pair(2)
        curses.init_pair(3, curses.COLOR_WHITE, -1)
        pairs["white"] = curses.color_pair(3)
    except curses.error:
        pass
    return pairs


@register("classic")
def render_classic(stdscr, trains):
    h, w = stdscr.getmaxyx()
    _draw_header(stdscr, 1)
    n_m, n_t, t_m, t_t = _train_values(trains)
    _draw_panels(stdscr, w, 20, 4, 6, n_m, n_t, t_m, t_t, CLASSIC_BOX)
    _draw_footer(stdscr, h, w)


@register("double")
def render_double(stdscr, trains):
    h, w = stdscr.getmaxyx()
    _draw_header(stdscr, 1)
    n_m, n_t, t_m, t_t = _train_values(trains)
    _draw_panels(
        stdscr,
        w,
        20,
        4,
        6,
        n_m,
        n_t,
        t_m,
        t_t,
        {"tl": "+", "tr": "+", "bl": "+", "br": "+", "h": "=", "v": "|"},
    )
    _draw_footer(stdscr, h, w, "=")


@register("minimal")
def render_minimal(stdscr, trains):
    h, w = stdscr.getmaxyx()
    _draw_header(stdscr, 1)
    n_m, n_t, t_m, t_t = _train_values(trains)
    row_y = 7
    gap = max((w - 28) // 2, 0)
    for x, title, minutes, arrival in (
        (gap, "NEXT", n_m, n_t),
        (gap + 14, "THEN", t_m, t_t),
    ):
        stdscr.addstr(row_y, x, title, curses.A_BOLD)
        stdscr.addstr(row_y + 2, x + max((6 - len(minutes)) // 2, 0), minutes, curses.A_BOLD)
        stdscr.addstr(row_y + 3, x, "minutes")
        stdscr.addstr(row_y + 4, x, arrival)
    _draw_footer(stdscr, h, w, " ")


@register("kindle")
def render_kindle(stdscr, trains):
    h, w = stdscr.getmaxyx()
    rule = "-" * max(min(w - 2, 50), 10)
    center_text(stdscr, 0, rule)
    _draw_header(stdscr, 1)
    center_text(stdscr, 4, rule)
    n_m, n_t, t_m, t_t = _train_values(trains)
    _draw_panels(stdscr, w, 20, 4, 6, n_m, n_t, t_m, t_t, CLASSIC_BOX)
    center_text(stdscr, 12, rule)
    _draw_footer(stdscr, h, w, "-")


@register("greenline")
def render_greenline(stdscr, trains):
    h, w = stdscr.getmaxyx()
    colors = _init_color_pairs()
    green = colors.get("green", 0)
    _draw_header(stdscr, 1, route_attr=green)
    n_m, n_t, t_m, t_t = _train_values(trains)
    _draw_panels(stdscr, w, 20, 4, 6, n_m, n_t, t_m, t_t, CLASSIC_BOX)
    _draw_footer(stdscr, h, w)


@register("ticker")
def render_ticker(stdscr, trains):
    h, w = stdscr.getmaxyx()
    _draw_header(stdscr, 1)
    n_m, n_t, t_m, t_t = _train_values(trains)
    next_line = f"< NEXT | {n_m} min | {n_t} >"
    then_line = f"< THEN | {t_m} min | {t_t} >"
    center_text(stdscr, 6, next_line, curses.A_BOLD)
    center_text(stdscr, 8, then_line, curses.A_BOLD)
    center_text(stdscr, 10, "=" * min(len(next_line), max(w - 1, 1)))
    _draw_footer(stdscr, h, w, "-")


@register("boston")
def render_boston(stdscr, trains):
    colors = _init_color_pairs()
    gold = colors.get("gold", curses.A_BOLD)
    _render_cap_style(
        stdscr,
        trains,
        BOSTON_CAP,
        footer_char="#",
        cap_attr=gold,
        route_attr=gold,
    )


@register("mbta")
def render_mbta(stdscr, trains):
    colors = _init_color_pairs()
    green = colors.get("green", curses.A_BOLD)
    _render_cap_style(
        stdscr,
        trains,
        MBTA_CAP,
        footer_char="-",
        cap_attr=green,
        route_attr=green,
    )


@register("custom")
def render_custom(stdscr, trains):
    cap_lines = settings.cap_banner or make_bevel_caps(settings.custom_message)
    _render_cap_style(
        stdscr,
        trains,
        cap_lines,
        footer_char="-",
        cap_attr=curses.A_BOLD,
    )
