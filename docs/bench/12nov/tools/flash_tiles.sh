#!/usr/bin/env bash
# Flash the tile firmware (panel_sync/build) to every tile on USB, then check
# each one's display ticks (bench 12 Nov 2026). Boards are named by their USB
# serial number (the MAC), never by port; the Pico and unknown boards are
# skipped. Plug tiles in one at a time on a laptop port, not a loaded hub.
#
# Usage, from Led_Matrix, after `idf.py build` exited 0:
#   bash docs/bench/12nov/tools/flash_tiles.sh --dry-run   # show, flash nothing
#   bash docs/bench/12nov/tools/flash_tiles.sh [SKIP...]   # SKIP: tile numbers
cd "$(dirname "$0")/../../../.." || exit 1

declare -A NODE=(
  [9C:13:9E:F1:30:70]=1 [B8:F8:62:D8:09:F8]=2 [B8:F8:62:D8:05:EC]=3
  [9C:13:9E:F1:2E:B0]=4 [9C:13:9E:F1:2F:70]=5 [9C:13:9E:F1:2F:E4]=6
  [9C:13:9E:F1:2C:70]=7 [9C:13:9E:F1:2B:D0]=8 [B8:F8:62:D8:0E:D8]=9)

dry=0
skip=" "
for arg in "$@"; do
  if [ "$arg" = "--dry-run" ]; then dry=1; else skip="$skip$arg "; fi
done

todo=()
for port in $(ls /dev/ttyACM* 2>/dev/null); do
  serial=$(udevadm info -q property -n "$port" | grep ID_SERIAL_SHORT | cut -d= -f2)
  node="${NODE[$serial]:-}"
  if [ -z "$node" ]; then echo "$port $serial: not a tile, skipped"; continue; fi
  if [[ "$skip" == *" $node "* ]]; then echo "$port tile $node: skipped"; continue; fi
  echo "$port tile $node: to flash"
  todo+=("$port:$node")
done
[ "$dry" = 1 ] && exit 0
[ ${#todo[@]} -eq 0 ] && { echo "no tile to flash"; exit 0; }

. ~/esp/esp-idf/export.sh >/dev/null 2>&1
for entry in "${todo[@]}"; do
  port=${entry%%:*}; node=${entry##*:}
  (cd panel_sync && idf.py -p "$port" flash 2>&1 | grep -E 'Hard resetting|rror' | tail -1
   exit "${PIPESTATUS[0]}")
  echo "tile $node flash exit=$?"
done

sleep 4
log=$(mktemp -d)
for entry in "${todo[@]}"; do
  port=${entry%%:*}; node=${entry##*:}
  .venv/bin/python docs/bench/12nov/serial_log.py 13 "$port=$log/tile$node.log"
  echo "tile $node: $(grep -a 'DE ticks' "$log/tile$node.log" | tail -1)"
done
