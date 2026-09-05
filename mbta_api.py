import requests

import config
from ssl_certs import get_requests_verify


def api_get(path, params=None):
    params = dict(params or {})
    if config.API_KEY:
        params["api_key"] = config.API_KEY

    url = f"{config.BASE_URL}{path}"
    verify, _ = get_requests_verify()
    response = requests.get(url, params=params, timeout=10, verify=verify)
    response.raise_for_status()
    return response.json()


def sanitize_error(message):
    if config.API_KEY:
        message = message.replace(config.API_KEY, "***")
    return message
