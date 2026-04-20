#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

build_one() {
  local app_dir="$1"
  local pro_file="$2"
  mkdir -p "$app_dir/build"
  pushd "$app_dir/build" >/dev/null
  qmake "../$pro_file"
  make -j"$(nproc)"
  popd >/dev/null
}

build_one "$ROOT_DIR/qt/touch_debug" "touch_debug.pro"
build_one "$ROOT_DIR/qt/touch_calibrate" "touch_calibrate.pro"
