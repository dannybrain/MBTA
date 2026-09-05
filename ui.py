import curses

from settings import settings
from styles import get_renderer


def draw_screen(stdscr, trains):
    curses.curs_set(0)
    stdscr.clear()

    renderer = get_renderer(settings.style)
    renderer(stdscr, trains)

    stdscr.refresh()
