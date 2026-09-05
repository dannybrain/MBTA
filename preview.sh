#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PYTHON="${HOME}/.venv/3.12/bin/python3"

if [[ ! -x "$PYTHON" ]]; then
  echo "Missing Python venv: $PYTHON" >&2
  echo "Expected ~/.venv/3.12/bin/python3" >&2
  exit 1
fi

exec "$PYTHON" "$SCRIPT_DIR/epaper_simulator.py" "$@"
