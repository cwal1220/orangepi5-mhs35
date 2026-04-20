#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
MOD_DIR="$ROOT_DIR/kernel/ads7846_poll"

make -C "/lib/modules/$(uname -r)/build" M="$MOD_DIR" modules
