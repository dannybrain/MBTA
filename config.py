import os

# Set MBTA_API_KEY in your environment, or assign here for local use only (do not commit real keys).
API_KEY = os.environ.get("MBTA_API_KEY", "")

BASE_URL = "https://api-v3.mbta.com"

DESTINATION = "Government Center"

# Green-C: 0 = Cleveland Circle (outbound), 1 = Government Center (inbound)
DIRECTION_ID = 1

REFRESH_SECONDS = 30

STOP_ID = "place-sumav"
ROUTE = "Green-C"
