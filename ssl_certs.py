import os
from pathlib import Path

import certifi


def get_requests_verify(configured_path=None):
    for key in ("REQUESTS_CA_BUNDLE", "SSL_CERT_FILE", "CURL_CA_BUNDLE"):
        env_path = os.environ.get(key)
        if env_path and Path(env_path).is_file():
            return env_path, f"env:{key}"

    if configured_path and Path(configured_path).is_file():
        return configured_path, "config:CA_BUNDLE"

    return certifi.where(), "certifi"
