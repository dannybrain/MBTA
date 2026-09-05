#!/usr/bin/env python3
"""Shared layout constants for the 540x960 e-paper display."""

PANEL_W = 540
PANEL_H = 960
MARGIN = 16
CONTENT_W = PANEL_W - MARGIN * 2

TOP_PAD = 100
BOTTOM_PAD = 20
SECTION_GAP = 12

BANNER_H = 56
ROUTE_H = 56
WEATHER_H = 60
STATUS_H = 48
FOOTER_H = 44

# Rules sit between every section (6 rules). Remaining height goes to train panels.
FIXED_H = (
    TOP_PAD
    + BANNER_H
    + ROUTE_H
    + WEATHER_H
    + STATUS_H * 2
    + FOOTER_H
    + BOTTOM_PAD
    + 6 * (1 + SECTION_GAP)
)
PANEL_TRAIN_H = PANEL_H - FIXED_H
