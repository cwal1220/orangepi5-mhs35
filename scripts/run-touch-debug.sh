#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
FB_DEVICE="${FB_DEVICE:-/dev/fb1}"
APP="$ROOT_DIR/qt/touch_debug/build/touch_debug"
USE_SUDO="${TOUCH_UI_USE_SUDO:-1}"
AUTO_LOAD_TOUCH="${TOUCH_UI_AUTO_LOAD_TOUCH:-1}"

resolve_touch_device() {
  if [[ -n "${TOUCH_DEVICE:-}" ]]; then
    printf '%s\n' "$TOUCH_DEVICE"
    return 0
  fi

  "$ROOT_DIR/scripts/resolve-touch-device.sh" 2>/dev/null || true
}

maybe_load_touch_driver() {
  if [[ "$AUTO_LOAD_TOUCH" != "1" ]]; then
    return 0
  fi

  if [[ "$(id -u)" -eq 0 && -x "$ROOT_DIR/scripts/load-ads7846-poll.sh" ]]; then
    "$ROOT_DIR/scripts/load-ads7846-poll.sh" >/dev/null 2>&1 || true
  fi
}

if [[ ! -x "$APP" ]]; then
  echo "Missing $APP. Run ./scripts/build-qt-tools.sh first." >&2
  exit 1
fi

if [[ "$USE_SUDO" == "1" && "$(id -u)" -ne 0 ]]; then
  exec sudo -E env TOUCH_UI_USE_SUDO=0 "$0"
fi

TOUCH_DEVICE="$(resolve_touch_device)"
if [[ -z "$TOUCH_DEVICE" ]]; then
  maybe_load_touch_driver
  TOUCH_DEVICE="$(resolve_touch_device)"
fi

if [[ -z "$TOUCH_DEVICE" || ! -e "$TOUCH_DEVICE" ]]; then
  echo "Missing touch device. Load ads7846_poll first or set TOUCH_DEVICE=/dev/input/eventX." >&2
  exit 1
fi

export QT_QPA_PLATFORM="linuxfb:fb=$FB_DEVICE:nographicsmodeswitch"
export QT_QPA_GENERIC_PLUGINS="evdevtouch"
export QT_QPA_EVDEV_TOUCHSCREEN_PARAMETERS="$TOUCH_DEVICE"
export XDG_RUNTIME_DIR="/tmp/runtime-root"
mkdir -p "$XDG_RUNTIME_DIR"
chmod 700 "$XDG_RUNTIME_DIR" 2>/dev/null || true

exec "$APP"
