#!/usr/bin/env bash
set -euo pipefail

preferred_names=(
  "ADS7846 Poll Touchscreen"
  "ADS7846 Touchscreen"
)

read_name() {
  local event_path="$1"
  local name_file

  for name_file in \
    "/sys/class/input/$event_path/device/name" \
    "/sys/class/input/$event_path/device/device/name"; do
    if [[ -r "$name_file" ]]; then
      cat "$name_file"
      return 0
    fi
  done

  return 1
}

find_by_name() {
  local wanted="$1"
  local event_path name

  for event_path in /sys/class/input/event*; do
    [[ -e "$event_path" ]] || continue
    name="$(read_name "$(basename "$event_path")" 2>/dev/null || true)"
    if [[ "$name" == "$wanted" ]]; then
      printf '/dev/input/%s\n' "$(basename "$event_path")"
      return 0
    fi
  done

  return 1
}

for name in "${preferred_names[@]}"; do
  if dev="$(find_by_name "$name")"; then
    printf '%s\n' "$dev"
    exit 0
  fi
done

for dev in /dev/input/by-path/*spi*event; do
  [[ -e "$dev" ]] || continue
  printf '%s\n' "$dev"
  exit 0
done

echo "Unable to find a touchscreen input device." >&2
exit 1
