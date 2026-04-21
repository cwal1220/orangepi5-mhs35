#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
KERNEL_RELEASE="${KERNEL_RELEASE:-$(uname -r)}"
MODULE_SRC="$ROOT_DIR/kernel/ads7846_poll/ads7846_poll.ko"
MODULE_DST_DIR="/lib/modules/$KERNEL_RELEASE/extra"
MODULE_DST="$MODULE_DST_DIR/ads7846_poll.ko"
LOADER_DST="/usr/local/sbin/orangepi5-load-ads7846-poll.sh"
SERVICE_DST="/etc/systemd/system/orangepi5-ads7846-poll.service"

if [[ $EUID -ne 0 ]]; then
  echo "Run this script as root." >&2
  exit 1
fi

if [[ ! -f "$MODULE_SRC" ]]; then
  echo "Missing $MODULE_SRC. Run ./scripts/build-kernel-module.sh first." >&2
  exit 1
fi

install -d -m 0755 "$MODULE_DST_DIR"
install -m 0644 "$MODULE_SRC" "$MODULE_DST"
depmod "$KERNEL_RELEASE"

install -m 0755 "$ROOT_DIR/scripts/load-ads7846-poll.sh" "$LOADER_DST"

cat > "$SERVICE_DST" <<EOF
[Unit]
Description=Bind ads7846_poll touchscreen driver for Orange Pi 5 MHS35
DefaultDependencies=no
After=local-fs.target systemd-modules-load.service
Before=multi-user.target
ConditionPathExists=/sys/bus/spi/devices/spi4.0

[Service]
Type=oneshot
ExecStart=$LOADER_DST
RemainAfterExit=yes

[Install]
WantedBy=multi-user.target
EOF

systemctl daemon-reload
systemctl enable orangepi5-ads7846-poll.service
systemctl restart orangepi5-ads7846-poll.service
