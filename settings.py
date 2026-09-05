from dataclasses import dataclass

import config


@dataclass
class RuntimeSettings:
    stop_id: str = config.STOP_ID
    stop_name: str = "Summit Avenue"
    route: str = config.ROUTE
    direction_id: int = config.DIRECTION_ID
    destination: str = config.DESTINATION
    style: str = "classic"
    custom_message: str = ""
    cap_banner: list[str] | None = None


settings = RuntimeSettings()
