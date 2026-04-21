#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
MOD_DIR="$ROOT_DIR/kernel/ads7846_poll"
SPI_DEV="${SPI_DEV:-spi4.0}"
DRIVER="${DRIVER:-ads7846_poll}"
LOCAL_MODULE="$MOD_DIR/ads7846_poll.ko"
INSTALLED_MODULE="/lib/modules/$(uname -r)/extra/ads7846_poll.ko"
MODULE="${MODULE:-}"

if [[ -z "$MODULE" ]]; then
  if [[ -f "$LOCAL_MODULE" ]]; then
    MODULE="$LOCAL_MODULE"
  else
    MODULE="$INSTALLED_MODULE"
  fi
fi

if [[ $EUID -ne 0 ]]; then
  echo "Run this script as root." >&2
  exit 1
fi

if [[ ! -e "/sys/bus/spi/devices/$SPI_DEV" ]]; then
  echo "Missing /sys/bus/spi/devices/$SPI_DEV. Is the overlay loaded?" >&2
  exit 1
fi

if [[ ! -f "$MODULE" ]]; then
  echo "Missing $MODULE. Build the module first or install it into $INSTALLED_MODULE." >&2
  exit 1
fi

modprobe -r ads7846 2>/dev/null || true
modprobe -r spidev 2>/dev/null || true

if [[ -L "/sys/bus/spi/devices/$SPI_DEV/driver" ]]; then
  current_driver="$(basename "$(readlink -f "/sys/bus/spi/devices/$SPI_DEV/driver")")"
  if [[ "$current_driver" == "$DRIVER" ]]; then
    echo "$SPI_DEV is already bound to $DRIVER"
    exit 0
  fi
  echo "$SPI_DEV" > "/sys/bus/spi/drivers/$current_driver/unbind" 2>/dev/null || true
fi

rmmod "$DRIVER" 2>/dev/null || true

insmod "$MODULE"
printf '%s' "$DRIVER" > "/sys/bus/spi/devices/$SPI_DEV/driver_override"
echo "$SPI_DEV" > "/sys/bus/spi/drivers/$DRIVER/bind"
printf '\n' > "/sys/bus/spi/devices/$SPI_DEV/driver_override"
