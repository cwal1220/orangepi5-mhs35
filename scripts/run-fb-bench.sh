#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
FB_DEVICE="${FB_DEVICE:-/dev/fb1}"
DURATION="${DURATION:-8}"
APP="$ROOT_DIR/bench/fb_bench"

if [[ ! -x "$APP" ]]; then
  echo "Missing $APP. Run ./scripts/build-fb-bench.sh first." >&2
  exit 1
fi

exec "$APP" "$FB_DEVICE" "$DURATION"
