#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
FB_DEVICE="${FB_DEVICE:-/dev/fb1}"
TOUCH_DEVICE="${TOUCH_DEVICE:-$("$ROOT_DIR/scripts/resolve-touch-device.sh")}"
APP="$ROOT_DIR/qt/touch_debug/build/touch_debug"

if [[ ! -x "$APP" ]]; then
  echo "Missing $APP. Run ./scripts/build-qt-tools.sh first." >&2
  exit 1
fi

if [[ ! -e "$TOUCH_DEVICE" ]]; then
  echo "Missing touch device $TOUCH_DEVICE." >&2
  exit 1
fi

export QT_QPA_PLATFORM="linuxfb:fb=$FB_DEVICE"
export QT_QPA_GENERIC_PLUGINS="evdevtouch"
export QT_QPA_EVDEV_TOUCHSCREEN_PARAMETERS="$TOUCH_DEVICE"

exec "$APP"
