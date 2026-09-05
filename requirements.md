# MBTA eInk Train Display

## Goal

Display the next two Green Line C trains arriving at Summit Avenue station heading toward Government Center.

The display should resemble a minimal Kindle screen.

Example:

----------------------------------------------------

Summit Ave → Government Center

        3 min          11 min

Updated
08:42:17

----------------------------------------------------

## Data source

MBTA V3 API

https://api-v3.mbta.com/

Authentication

API Key

## Station

Summit Avenue

Stop ID

70206

Direction

Inbound

Destination

Government Center

Route

Green-C

## Refresh

Every 30 seconds.

## Language

Python 3.12+

## Dependencies

requests

rich (optional)

No pandas.

Use the standard curses module for the terminal UI.

## Files

config.py

Contains:

API_KEY

BASE_URL

STOP_ID

ROUTE

DESTINATION

mbta.py

Responsible for:

Calling the MBTA API

Returning the next arrivals

ui.py

Responsible for:

Drawing the terminal interface using curses

main.py

Main loop:

Fetch arrivals

Update display

Sleep

Repeat

## Display

Show:

Title

Current time

Next train

Following train

Minutes until arrival

If no prediction exists:

"No upcoming trains"

## Error handling

Display:

"No Internet"

"API Error"

instead of crashing.

## Future enhancements

Display delays

Display alerts

Display vehicle occupancy

Display destination changes

Port to ESP32 / LilyGO T5 Paper S3 Lite

Replace curses with eInk drawing library
