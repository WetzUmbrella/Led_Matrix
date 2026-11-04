#!/usr/bin/env bash
# Build every tile and Pico image for the bench day from the checked-out
# commit, for one WiFi network, and collect them into flashable folders.
#
# Usage (WSL or Linux, from the repo root, ESP-IDF v5.3 exported and
# PICO_SDK_PATH set):
#   SSID='our-router' PASS='secret' ./tools/build_bench_images.sh A out/bench-5nov
#   SSID='phone-hotspot' PASS='secret' ./tools/build_bench_images.sh B out/bench-5nov
#
# Network A builds the plain, hook 2, stress and suppression images; network
# B builds the plain and the test-build images (bench plan section 3.2).
# The SSID and password are written into the two git-ignored wifi_secrets.h
# files; nothing secret is committed. Each tile folder holds the three .bin
# files and flash_args, so a teammate flashes it with:
#   python -m esptool --chip esp32s3 -p COM<n> -b 460800 write_flash @flash_args
# (run from inside the folder). Never erase_flash: it wipes the board number.
set -euo pipefail

NET="${1:?network letter A or B}"
OUT="${2:?output folder}"
: "${SSID:?set SSID}"
: "${PASS:?set PASS}"
: "${PICO_SDK_PATH:?set PICO_SDK_PATH}"
command -v idf.py >/dev/null || { echo "export ESP-IDF first (. ~/esp/esp-idf/export.sh)"; exit 1; }

ROOT="$(git rev-parse --show-toplevel)"
TAG="$(git -C "$ROOT" describe --tags --always)"
mkdir -p "$OUT"
OUT="$(cd "$OUT" && pwd)"
LOG="$OUT/build_${NET}.log"
: > "$LOG"

write_secrets () {
    for f in "$ROOT/panel_sync/main/wifi_secrets.h" "$ROOT/ledwall_sync/wifi_secrets.h"; do
        sed -e "s|^#define ROUTER_SSID .*|#define ROUTER_SSID \"$SSID\"|" \
            -e "s|^#define ROUTER_PASS .*|#define ROUTER_PASS \"$PASS\"|" \
            "${f%.h}.example.h" > "$f"
    done
}

tile () { # name extra-idf-args...
    local name="$1"; shift
    local dir="$ROOT/panel_sync/build_bench_${name}_${NET}"
    echo "== tile $name ($NET)" | tee -a "$LOG"
    (cd "$ROOT/panel_sync" && idf.py -B "$dir" "$@" build >> "$LOG" 2>&1)
    local dest="$OUT/tile_${name}_${NET}_${TAG}"
    mkdir -p "$dest"
    cp "$dir/bootloader/bootloader.bin" "$dir/partition_table/partition-table.bin" \
       "$dir/panel_sync.bin" "$dir/flash_args" "$dest/"
    # flash_args uses paths relative to the build folder; flatten them.
    sed -i -e 's|bootloader/||' -e 's|partition_table/||' "$dest/flash_args"
    echo "   warnings: $(grep -c 'warning:' "$LOG" || true) (cumulative)" | tee -a "$LOG"
}

pico () { # name extra-cmake-args...
    local name="$1"; shift
    local dir="$ROOT/ledwall_sync/build_bench_${name}_${NET}"
    echo "== pico $name ($NET)" | tee -a "$LOG"
    rm -rf "$dir" && mkdir -p "$dir"
    (cd "$dir" && cmake .. "$@" >> "$LOG" 2>&1 && make -j4 >> "$LOG" 2>&1)
    cp "$dir/ledwall_sync.uf2" "$OUT/pico_${name}_${NET}_${TAG}.uf2"
}

write_secrets
if [ ! -f "$ROOT/panel_sync/sdkconfig" ]; then
    (cd "$ROOT/panel_sync" && idf.py set-target esp32s3 >> "$LOG" 2>&1)
fi

tile plain
pico plain
if [ "$NET" = "A" ]; then
    tile hook2 -DTN_RX_HOOK=2
    tile faultstress -DCMAKE_C_FLAGS=-DFAULT_STRESS
    tile tsstress -DCMAKE_C_FLAGS=-DTS_STRESS
    for ms in 1000 2000 4000 8000 10000; do
        pico "suppress${ms}" "-DCL_TEST_SUPPRESS_MS=${ms}"
    done
    # pico_tap needs no WiFi; built once with network A.
    dir="$ROOT/tools/pico_tap/build_bench"
    rm -rf "$dir" && mkdir -p "$dir"
    (cd "$dir" && cmake .. >> "$LOG" 2>&1 && make -j4 >> "$LOG" 2>&1)
    cp "$dir/pico_tap.uf2" "$OUT/pico_tap_${TAG}.uf2"
else
    tile vc4 -DCMAKE_C_FLAGS=-DTN_TEST_INJECT_FAULTS
    tile dump -DCMAKE_C_FLAGS=-DDE_DUMP_FRAMES
    tile profile -DCMAKE_C_FLAGS=-DDE_PROFILE
    tile slowprep -DCMAKE_C_FLAGS=-DDE_TEST_SLOW_PREP
    tile flasherase -DCMAKE_C_FLAGS=-DDE_TEST_FLASH_ERASE
fi

echo "Done: $(ls "$OUT" | wc -l) items in $OUT; total compiler warnings: $(grep -c 'warning:' "$LOG" || true)"
echo "Tag: $TAG. Share $OUT (it contains no passwords: those stay in wifi_secrets.h)."
