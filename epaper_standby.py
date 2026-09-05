"""Standby home screen preview (v3.0 — TAP HERE + rotating quotes)."""

from __future__ import annotations

from datetime import datetime
from pathlib import Path

import pygame

PANEL_W = 960
PANEL_H = 540
FG = (20, 20, 20)
BG = (245, 245, 240)

QUOTES: list[tuple[str, str]] = [
    ("The journey of a thousand miles begins with one step.", "Lao Tzu"),
    ("Knowing others is wisdom; knowing yourself is enlightenment.", "Lao Tzu"),
    ("It is not that we have a short time to live, but that we waste much of it.", "Seneca"),
    ("We suffer more often in imagination than in reality.", "Seneca"),
    ("The unexamined life is not worth living.", "Socrates"),
    ("Happiness depends upon ourselves.", "Aristotle"),
    ("The only true wisdom is in knowing you know nothing.", "Socrates"),
    ("He who has a why to live can bear almost any how.", "Nietzsche"),
    (
        "In the midst of winter, I found there was, within me, an invincible summer.",
        "Camus",
    ),
    ("The art of being wise is the art of knowing what to overlook.", "William James"),
    (
        "Do not dwell in the past; do not dream of the future. "
        "Concentrate the mind on the present moment.",
        "Buddha",
    ),
    ("What we think, we become.", "Buddha"),
    ("Patience is bitter, but its fruit is sweet.", "Aristotle"),
    ("Life can only be understood backwards; but it must be lived forwards.", "Kierkegaard"),
    ("Simplicity is the ultimate sophistication.", "Leonardo da Vinci"),
    (
        "The best time to plant a tree was twenty years ago. The second best time is now.",
        "Chinese proverb",
    ),
]


def _hand_font(size: int = 24) -> pygame.font.Font:
    for name in ("Bradley Hand", "Snell Roundhand", "Marker Felt", "Chalkboard SE"):
        if name.lower() in pygame.font.get_fonts():
            return pygame.font.SysFont(name, size)
    return pygame.font.SysFont("Georgia", size, italic=True)


def _wrap_text(font: pygame.font.Font, text: str, max_width: int) -> list[str]:
    words = text.split()
    lines: list[str] = []
    current = ""
    for word in words:
        trial = word if not current else f"{current} {word}"
        if font.size(trial)[0] <= max_width:
            current = trial
        else:
            if current:
                lines.append(current)
            current = word
    if current:
        lines.append(current)
    return lines[:2]


def render_standby_surface(
    fonts: dict[str, pygame.font.Font],
    battery_percent: int,
    quote_index: int = 0,
    last_checked: datetime | None = None,
) -> pygame.Surface:
    surface = pygame.Surface((PANEL_W, PANEL_H))
    surface.fill(BG)

    if 0 <= battery_percent <= 100:
        batt = fonts["body"].render(f"{battery_percent}%", True, FG)
        surface.blit(batt, batt.get_rect(topright=(PANEL_W - 16, 16)))

    btn_w, btn_h = 620, 200
    bx = (PANEL_W - btn_w) // 2
    by = (PANEL_H - 130 - btn_h) // 2
    pygame.draw.rect(surface, FG, (bx, by, btn_w, btn_h), 3)
    pygame.draw.rect(surface, FG, (bx + 4, by + 4, btn_w - 8, btn_h - 8), 2)
    tap_font = pygame.font.SysFont("Menlo", 96, bold=True)
    tap = tap_font.render("TAP HERE", True, FG)
    surface.blit(tap, tap.get_rect(center=(PANEL_W // 2, by + btn_h // 2)))

    band_y = PANEL_H - 130
    pygame.draw.line(surface, FG, (16, band_y - 6), (PANEL_W - 16, band_y - 6), 1)

    if last_checked:
        checked = fonts["body"].render(
            f"Last checked {last_checked.strftime('%a %I:%M %p').replace(' 0', ' ')}",
            True,
            FG,
        )
    else:
        checked = fonts["body"].render("Not checked yet", True, FG)
    surface.blit(checked, checked.get_rect(midtop=(PANEL_W // 2, band_y + 4)))

    quote_text, author = QUOTES[quote_index % len(QUOTES)]
    hand = _hand_font(24)
    y = band_y + 28
    for line in _wrap_text(hand, quote_text, PANEL_W - 80):
        rendered = hand.render(line, True, FG)
        surface.blit(rendered, rendered.get_rect(midtop=(PANEL_W // 2, y)))
        y += 28

    sig = _hand_font(22).render(f"— {author}", True, FG)
    surface.blit(sig, sig.get_rect(midtop=(PANEL_W // 2, band_y + 98)))
    return surface


def render_loading_surface(
    fonts: dict[str, pygame.font.Font],
    battery_percent: int,
) -> pygame.Surface:
    surface = pygame.Surface((PANEL_W, PANEL_H))
    surface.fill(BG)

    if 0 <= battery_percent <= 100:
        batt = fonts["body"].render(f"{battery_percent}%", True, FG)
        surface.blit(batt, batt.get_rect(topright=(PANEL_W - 16, 16)))

    box_w, box_h = 620, 200
    bx = (PANEL_W - box_w) // 2
    by = (PANEL_H - box_h) // 2
    pygame.draw.rect(surface, FG, (bx, by, box_w, box_h), 3)
    pygame.draw.rect(surface, FG, (bx + 4, by + 4, box_w - 8, box_h - 8), 2)

    loading_font = pygame.font.SysFont("Menlo", 96, bold=True)
    loading = loading_font.render("Loading...", True, FG)
    surface.blit(loading, loading.get_rect(center=(PANEL_W // 2, by + box_h // 2 - 28)))

    sub_font = pygame.font.SysFont("Menlo", 32, bold=True)
    sub = sub_font.render("Connecting & fetching trains", True, FG)
    surface.blit(sub, sub.get_rect(center=(PANEL_W // 2, by + box_h // 2 + 36)))
    return surface
