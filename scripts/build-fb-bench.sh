#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC="$ROOT_DIR/bench/fb_bench.c"
OUT="$ROOT_DIR/bench/fb_bench"

cc -O2 -Wall -Wextra -std=c11 "$SRC" -o "$OUT"
echo "built $OUT"
