#!/usr/bin/env python3
"""Mac preview of the LilyGO T5 e-paper MBTA display (540×960 portrait)."""

from __future__ import annotations

import argparse
import subprocess
import sys
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path

import pygame

ROOT = Path(__file__).resolve().parent
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

import config
from epaper_layout import (
    BANNER_H,
    BOTTOM_PAD,
    CONTENT_W,
    FOOTER_H,
    MARGIN,
    PANEL_H,
    PANEL_TRAIN_H,
    PANEL_W,
    ROUTE_H,
    SECTION_GAP,
    STATUS_H,
    TOP_PAD,
    WEATHER_H,
)
from epaper_predictions import ArrivalEstimate, get_summit_arrivals
from epaper_service_window import (
    ServiceWindow,
    fetch_service_window,
    inbound_last_train_line,
    night_mode_line,
    resume_short_line,
)
from epaper_standby import render_loading_surface, render_standby_surface
from epaper_weather import TomorrowForecast, WeatherSnapshot, fetch_brookline_weather, fetch_tomorrow_forecast

REFRESH_MS = config.REFRESH_SECONDS * 1000
PREVIEW_PATH = ROOT / "preview.png"
BG = (245, 245, 240)
FG = (20, 20, 20)
ROUTE_LINE = "Summit Avenue -> Government Center"
VALUE_X = PANEL_W // 2


@dataclass
class TrainPanel:
    label: str
    minutes: int | None
    clock_time: str
    source: str | None = None


@dataclass
class ScreenState:
    next_train: TrainPanel
    then_train: TrainPanel
    weather: WeatherSnapshot | None
    weather_error: str | None
    battery_percent: int
    battery_charging: bool
    status_error: str | None
    wifi_line: str
    updated: datetime


@dataclass
class InfoBoardState:
    clock_time: str
    tomorrow: TomorrowForecast | None
    service_window: ServiceWindow
    trains: list[ArrivalEstimate]
    status_error: str | None
    updated: datetime


@dataclass
class PreviewState:
    main: ScreenState
    info: InfoBoardState


@dataclass(frozen=True)
class ToggleButton:
    rect: pygame.Rect
    label: str


def load_fonts() -> dict[str, pygame.font.Font]:
    mono = "Menlo" if "menlo" in pygame.font.get_fonts() else "monospace"
    symbols = "apple symbols" if "apple symbols" in pygame.font.get_fonts() else mono
    return {
        "footer": pygame.font.SysFont(mono, 26),
        "body": pygame.font.SysFont(mono, 26),
        "route": pygame.font.SysFont(mono, 26),
        "banner": pygame.font.SysFont(mono, 32, bold=True),
        "panel_label": pygame.font.SysFont(mono, 28, bold=True),
        "minutes": pygame.font.SysFont(mono, 80, bold=True),
        "source": pygame.font.SysFont(mono, 18),
        "clock": pygame.font.SysFont(mono, 28),
        "weather_icon": pygame.font.SysFont(symbols, 32),
        "big_clock": pygame.font.SysFont(mono, 72, bold=True),
        "tt_row": pygame.font.SysFont(mono, 22),
        "tt_label": pygame.font.SysFont(mono, 22, bold=True),
        "toggle": pygame.font.SysFont(mono, 18, bold=True),
    }


def format_updated(when: datetime) -> str:
    clock = when.strftime("%I:%M %p").lstrip("0")
    return f"Updated {clock}"


def fetch_screen(
    battery_percent: int,
    battery_charging: bool,
    wifi_ssid: str,
) -> ScreenState:
    status_error = None
    weather_error = None
    weather = None
    trains_raw: list[ArrivalEstimate] = []

    try:
        trains_raw = get_summit_arrivals()
    except Exception as exc:
        status_error = str(exc)

    try:
        weather = fetch_brookline_weather()
    except Exception as exc:
        weather_error = str(exc)

    return ScreenState(
        next_train=TrainPanel("NEXT", trains_raw[0].minutes, trains_raw[0].clock_time,
                              trains_raw[0].source.value)
        if trains_raw
        else TrainPanel("NEXT", None, ""),
        then_train=TrainPanel("THEN", trains_raw[1].minutes, trains_raw[1].clock_time,
                              trains_raw[1].source.value)
        if len(trains_raw) > 1
        else TrainPanel("THEN", None, ""),
        weather=weather,
        weather_error=weather_error,
        battery_percent=battery_percent,
        battery_charging=battery_charging,
        status_error=status_error,
        wifi_line="connected",
        updated=datetime.now(),
    )


def fetch_info_board() -> InfoBoardState:
    status_error = None
    trains: list[ArrivalEstimate] = []
    tomorrow = None
    service = ServiceWindow()

    try:
        trains = get_summit_arrivals()
    except Exception as exc:
        status_error = str(exc)

    try:
        tomorrow = fetch_tomorrow_forecast()
    except Exception:
        tomorrow = None

    try:
        service = fetch_service_window()
    except Exception as exc:
        service = ServiceWindow(error=str(exc))

    clock = datetime.now().strftime("%I:%M %p").lstrip("0")
    return InfoBoardState(
        clock_time=clock,
        tomorrow=tomorrow,
        service_window=service,
        trains=trains,
        status_error=status_error,
        updated=datetime.now(),
    )


def fetch_preview(
    battery_percent: int,
    battery_charging: bool,
    wifi_ssid: str,
) -> PreviewState:
    return PreviewState(
        main=fetch_screen(battery_percent, battery_charging, wifi_ssid),
        info=fetch_info_board(),
    )


def draw_rule(surface: pygame.Surface, y: int) -> None:
    x = (PANEL_W - CONTENT_W) // 2
    pygame.draw.line(surface, FG, (x, y), (x + CONTENT_W, y), 1)


def section_rule(y: int) -> int:
    return y + 1 + SECTION_GAP


def draw_section_center(
    surface: pygame.Surface,
    text: str,
    section_y: int,
    section_h: int,
    font: pygame.font.Font,
) -> None:
    rendered = font.render(text, True, FG)
    surface.blit(rendered, rendered.get_rect(center=(PANEL_W // 2, section_y + section_h // 2)))


def draw_panel(
    surface: pygame.Surface,
    fonts: dict[str, pygame.font.Font],
    x: int,
    y: int,
    w: int,
    h: int,
    panel: TrainPanel,
) -> None:
    pygame.draw.rect(surface, FG, (x, y, w, h), 2)
    pygame.draw.line(surface, FG, (x + 12, y + 44), (x + w - 12, y + 44), 1)

    label = fonts["panel_label"].render(panel.label, True, FG)
    surface.blit(label, label.get_rect(midtop=(x + w // 2, y + 12)))

    if panel.minutes is not None:
        mid_y = y + (h * 40) // 100
        mins = fonts["minutes"].render(str(panel.minutes), True, FG)
        surface.blit(mins, mins.get_rect(center=(x + w // 2, mid_y)))
        if panel.source:
            src = fonts["source"].render(panel.source, True, FG)
            surface.blit(src, src.get_rect(midtop=(x + w // 2, mid_y + mins.get_height() // 2 + 4)))
        mins_label = fonts["body"].render("minutes", True, FG)
        surface.blit(mins_label, mins_label.get_rect(midtop=(x + w // 2, y + (h * 62) // 100)))
        clock = fonts["clock"].render(panel.clock_time, True, FG)
        surface.blit(clock, clock.get_rect(midtop=(x + w // 2, y + (h * 76) // 100)))
    else:
        empty = fonts["clock"].render("No train", True, FG)
        surface.blit(empty, empty.get_rect(center=(x + w // 2, y + h // 2)))


def draw_row_label(surface: pygame.Surface, label: str, mid_y: int, font: pygame.font.Font) -> None:
    text = font.render(label, True, FG)
    surface.blit(text, text.get_rect(midleft=(MARGIN, mid_y)))


def draw_row_value(surface: pygame.Surface, value: str, mid_y: int, font: pygame.font.Font) -> None:
    text = font.render(value, True, FG)
    surface.blit(text, text.get_rect(midleft=(VALUE_X, mid_y)))


def draw_weather_line(
    surface: pygame.Surface,
    fonts: dict[str, pygame.font.Font],
    y: int,
    state: ScreenState,
) -> None:
    mid_y = y + WEATHER_H // 2
    font = fonts["body"]
    if state.status_error:
        draw_row_label(surface, "Weather:", mid_y, font)
        draw_row_value(surface, state.status_error, mid_y, font)
        return
    if not state.weather:
        draw_row_label(surface, "Weather:", mid_y, font)
        draw_row_value(surface, "unavailable" if state.weather_error else "--", mid_y, font)
        return

    draw_row_label(surface, "Weather:", mid_y, font)
    icon = fonts["weather_icon"].render(state.weather.icon, True, FG)
    values = font.render(state.weather.values_text(), True, FG)
    ix = VALUE_X
    surface.blit(icon, icon.get_rect(midleft=(ix, mid_y)))
    surface.blit(values, values.get_rect(midleft=(ix + icon.get_width() + 8, mid_y)))


def draw_battery_line(
    surface: pygame.Surface,
    fonts: dict[str, pygame.font.Font],
    y: int,
    percent: int,
    charging: bool,
) -> None:
    mid_y = y + STATUS_H // 2
    font = fonts["body"]
    draw_row_label(surface, "Battery:", mid_y, font)

    value_text = f"{percent}% charging" if charging else f"{percent}%"
    value = font.render(value_text, True, FG)
    right_x = PANEL_W - MARGIN
    bar_x = VALUE_X
    bar_y = mid_y - 10
    bar_w = right_x - value.get_width() - 10 - bar_x
    if bar_w > 40:
        pygame.draw.rect(surface, FG, (bar_x, bar_y, bar_w, 20), 2)
        fill_w = max((bar_w - 4) * percent // 100, 0)
        if fill_w:
            pygame.draw.rect(surface, FG, (bar_x + 2, bar_y + 2, fill_w, 16))
        surface.blit(value, value.get_rect(midright=(right_x, mid_y)))
    else:
        draw_row_value(surface, value_text, mid_y, font)


def draw_wifi_line(
    surface: pygame.Surface,
    fonts: dict[str, pygame.font.Font],
    y: int,
    wifi_line: str,
) -> None:
    mid_y = y + STATUS_H // 2
    draw_row_label(surface, "WiFi:", mid_y, fonts["body"])
    draw_row_value(surface, wifi_line, mid_y, fonts["body"])


def draw_tt_row(
    surface: pygame.Surface,
    fonts: dict[str, pygame.font.Font],
    label: str,
    value: str,
    y: int,
    row_h: int = 22,
) -> None:
    label_font = fonts["tt_label"]
    value_font = fonts["tt_row"]
    label_x = MARGIN + 8
    surface.blit(label_font.render(label, True, FG), (label_x, y + 2))
    value_x = label_x + 120
    surface.blit(value_font.render(value, True, FG), (value_x, y + 2))


def draw_info_board_frame(state: InfoBoardState, fonts: dict[str, pygame.font.Font]) -> pygame.Surface:
    surface = pygame.Surface((PANEL_W, PANEL_H))
    surface.fill(BG)

    split_y = (PANEL_H * 2) // 3
    content_w = CONTENT_W
    row_h = 22
    y = 48

    draw_section_center(surface, "TOMORROW", y, BANNER_H, fonts["banner"])
    y += BANNER_H + 6
    draw_rule(surface, y)
    y += 8

    if state.tomorrow:
        draw_section_center(surface, state.tomorrow.day_label, y, 26, fonts["body"])
        y += 28

    draw_rule(surface, y)
    y += 8

    clock = fonts["big_clock"].render(state.clock_time, True, FG)
    surface.blit(clock, clock.get_rect(center=(PANEL_W // 2, y + 28)))
    y += 58
    draw_rule(surface, y)
    y += 8

    draw_tt_row(surface, fonts, "Weather:", "", y, row_h)
    y += row_h

    if state.tomorrow:
        fc = state.tomorrow
        weather_top = y
        icon = fonts["weather_icon"].render(fc.icon, True, FG)
        surface.blit(icon, icon.get_rect(center=(PANEL_W // 2, y + 18)))
        y += 46

        draw_tt_row(surface, fonts, "Max:", f"{fc.temp_high_c}C", y, row_h)
        y += row_h
        draw_tt_row(surface, fonts, "Min:", f"{fc.temp_low_c}C", y, row_h)
        y += row_h
        if fc.humidity_percent >= 0:
            draw_tt_row(surface, fonts, "Humidity:", f"{fc.humidity_percent}%", y, row_h)
            y += row_h
        if fc.uv_index >= 0:
            draw_tt_row(surface, fonts, "UV:", str(fc.uv_index), y, row_h)
            y += row_h
        if fc.wind_kmh >= 0:
            draw_tt_row(surface, fonts, "Wind:", f"{fc.wind_kmh} km/h", y, row_h)
            y += row_h
        draw_rule(surface, y)
        y += 5
        draw_tt_row(surface, fonts, "Rain:", fc.rain_note, y, row_h)
        y += row_h + 4
        pygame.draw.rect(surface, FG, (MARGIN, weather_top - 4, content_w, y - weather_top + 4), 1)
    else:
        draw_section_center(surface, "Weather unavailable", y + 16, 32, fonts["body"])
        y += 40

    draw_rule(surface, split_y)
    ty = split_y + 6
    draw_section_center(surface, "TIMETABLE", ty, BANNER_H, fonts["banner"])
    ty += BANNER_H + 4
    draw_section_center(surface, "Summit Avenue · Green-C", ty, 18, fonts["tt_row"])
    ty += 18

    service_box_top = ty

    service = state.service_window
    service_lines = [
        inbound_last_train_line(service),
        night_mode_line(service),
        resume_short_line(service),
    ]
    has_service_lines = any(service_lines)
    if has_service_lines:
        for line in service_lines:
            if not line:
                continue
            text = fonts["tt_row"].render(line, True, FG)
            surface.blit(text, (MARGIN + 8, ty))
            ty += row_h
    else:
        loading = "Schedule loading..."
        if service.error:
            loading = service.error[:40]
        text = fonts["tt_row"].render(loading, True, FG)
        surface.blit(text, (MARGIN + 8, ty))
        ty += row_h

    if has_service_lines:
        pygame.draw.rect(surface, FG, (MARGIN, service_box_top - 2, content_w, ty - service_box_top + 6), 1)
    ty += 6
    draw_rule(surface, ty)
    ty += 8

    if state.status_error:
        draw_section_center(surface, state.status_error, ty, row_h, fonts["tt_row"])
        ty += row_h + 2
        draw_rule(surface, ty)
        ty += 8

    if state.trains:
        draw_section_center(surface, "min   time   destination   src", ty, row_h, fonts["tt_row"])
        ty += row_h
        draw_rule(surface, ty - 2)
        bottom_limit = PANEL_H - BOTTOM_PAD - FOOTER_H
        for train in state.trains[:8]:
            if ty + row_h >= bottom_limit:
                break
            line = f"  {train.minutes:2d}m {train.clock_time:>7}  Government Ctr  {train.source.value}"
            draw_section_center(surface, line, ty, row_h, fonts["tt_row"])
            ty += row_h

    draw_section_center(
        surface,
        format_updated(state.updated),
        PANEL_H - BOTTOM_PAD - FOOTER_H,
        FOOTER_H,
        fonts["footer"],
    )
    return surface


def render_frame(state: ScreenState, fonts: dict[str, pygame.font.Font]) -> pygame.Surface:
    surface = pygame.Surface((PANEL_W, PANEL_H))
    surface.fill(BG)

    y = TOP_PAD
    draw_section_center(surface, "GREEN C", y, BANNER_H, fonts["banner"])
    y += BANNER_H
    draw_rule(surface, y)
    y = section_rule(y)

    draw_section_center(surface, ROUTE_LINE, y, ROUTE_H, fonts["route"])
    y += ROUTE_H
    draw_rule(surface, y)
    y = section_rule(y)

    panel_w = (CONTENT_W - 20) // 2
    draw_panel(surface, fonts, MARGIN, y, panel_w, PANEL_TRAIN_H, state.next_train)
    draw_panel(surface, fonts, MARGIN + panel_w + 20, y, panel_w, PANEL_TRAIN_H, state.then_train)
    y += PANEL_TRAIN_H
    draw_rule(surface, y)
    y = section_rule(y)

    draw_weather_line(surface, fonts, y, state)
    y += WEATHER_H
    draw_rule(surface, y)
    y = section_rule(y)

    draw_battery_line(surface, fonts, y, state.battery_percent, state.battery_charging)
    y += STATUS_H
    draw_rule(surface, y)
    y = section_rule(y)

    draw_wifi_line(surface, fonts, y, state.wifi_line)
    y += STATUS_H
    draw_rule(surface, y)

    footer = format_updated(state.updated)
    draw_section_center(
        surface,
        footer,
        PANEL_H - BOTTOM_PAD - FOOTER_H,
        FOOTER_H,
        fonts["footer"],
    )
    return surface


def make_toggle_button(show_info: bool, win_w: int) -> ToggleButton:
    label = "Main screen" if show_info else "Info board"
    text_w = len(label) * 9 + 24
    rect = pygame.Rect(win_w - text_w - 12, 8, text_w, 32)
    return ToggleButton(rect=rect, label=label)


def draw_toggle_button(screen: pygame.Surface, button: ToggleButton, fonts: dict[str, pygame.font.Font]) -> None:
    pygame.draw.rect(screen, (230, 230, 225), button.rect, border_radius=6)
    pygame.draw.rect(screen, FG, button.rect, 2, border_radius=6)
    text = fonts["toggle"].render(button.label, True, FG)
    screen.blit(text, text.get_rect(center=button.rect.center))


def render_preview_frame(
    preview: PreviewState,
    fonts: dict[str, pygame.font.Font],
    show_info: bool,
) -> pygame.Surface:
    if show_info:
        return draw_info_board_frame(preview.info, fonts)
    return render_frame(preview.main, fonts)


def save_preview(surface: pygame.Surface, path: Path, open_file: bool) -> None:
    pygame.image.save(surface, str(path))
    print(f"Saved {path}")
    if open_file:
        subprocess.run(["open", str(path)], check=False)


def run_live(
    fonts: dict[str, pygame.font.Font],
    battery_percent: int,
    battery_charging: bool,
    wifi_ssid: str,
    scale: float,
    start_on_info: bool = False,
) -> None:
    win_w = int(PANEL_W * scale)
    win_h = int(PANEL_H * scale) + 48
    screen = pygame.display.set_mode((win_w, win_h))
    pygame.display.set_caption("MBTA E-Paper Preview (540×960) — T or button toggles info board")
    clock = pygame.time.Clock()
    preview = fetch_preview(battery_percent, battery_charging, wifi_ssid)
    last_fetch = pygame.time.get_ticks()
    show_info = start_on_info
    running = True

    while running:
        now = pygame.time.get_ticks()
        if now - last_fetch >= REFRESH_MS:
            preview = fetch_preview(battery_percent, battery_charging, wifi_ssid)
            last_fetch = now

        toggle = make_toggle_button(show_info, win_w)

        for event in pygame.event.get():
            if event.type == pygame.QUIT:
                running = False
            elif event.type == pygame.KEYDOWN and event.key in (pygame.K_q, pygame.K_ESCAPE):
                running = False
            elif event.type == pygame.KEYDOWN and event.key in (pygame.K_r,):
                preview = fetch_preview(battery_percent, battery_charging, wifi_ssid)
                last_fetch = now
            elif event.type == pygame.KEYDOWN and event.key in (pygame.K_t, pygame.K_SPACE):
                show_info = not show_info
            elif event.type == pygame.MOUSEBUTTONDOWN and event.button == 1:
                if toggle.rect.collidepoint(event.pos):
                    show_info = not show_info

        screen.fill((210, 210, 205))
        draw_toggle_button(screen, toggle, fonts)
        frame = render_preview_frame(preview, fonts, show_info)
        scaled = pygame.transform.scale(frame, (win_w, win_h - 48))
        screen.blit(scaled, (0, 48))
        pygame.display.flip()
        clock.tick(30)

    pygame.quit()


def main() -> None:
    parser = argparse.ArgumentParser(description="Preview the e-paper MBTA layout on your Mac.")
    parser.add_argument("--scale", type=float, default=0.85)
    parser.add_argument("--battery", type=int, default=85)
    parser.add_argument("--charging", action="store_true")
    parser.add_argument("--wifi-ssid", default="MyNetwork")
    parser.add_argument("--standby", action="store_true", help="Render v3.0 standby home screen")
    parser.add_argument(
        "--loading",
        action="store_true",
        help="Render loading screen shown after standby tap",
    )
    parser.add_argument("--live", action="store_true")
    parser.add_argument("--info", action="store_true", help="Render info board (use with --live: start on info board)")
    parser.add_argument("--output", type=Path, default=PREVIEW_PATH)
    parser.add_argument("--no-open", action="store_true")
    args = parser.parse_args()

    pygame.init()
    fonts = load_fonts()

    if args.standby:
        frame = render_standby_surface(fonts, args.battery)
        save_preview(frame, args.output, open_file=not args.no_open)
        pygame.quit()
        return

    if args.loading:
        frame = render_loading_surface(fonts, args.battery)
        save_preview(frame, args.output, open_file=not args.no_open)
        pygame.quit()
        return

    if args.live:
        run_live(fonts, args.battery, args.charging, args.wifi_ssid, args.scale, start_on_info=args.info)
        return

    preview = fetch_preview(args.battery, args.charging, args.wifi_ssid)
    frame = render_preview_frame(preview, fonts, show_info=args.info)
    save_preview(frame, args.output, open_file=not args.no_open)
    pygame.quit()


if __name__ == "__main__":
    main()
