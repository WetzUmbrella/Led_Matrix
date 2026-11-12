#!/usr/bin/env python3
"""Build per-tile packed assets from a GIF (T-B1-02, T-B1-03).

Owner: Jolyn (Buddy 1), tasks T-B1-02 and T-B1-03. Implemented by Ahsanul
(B4) for Jolyn, written 29 Oct 2026 in Jia Jun's (B5) session. Replaces
``panel_sync/tools/make_frames.py`` (mean delay, 30 fps cap).

T-B1-02 (resample and partition):
  * GIF delays are accumulated and mapped onto the 1,000,000 / 60 us grid by
    cumulative time: source frame i covers the ticks up to
    ceil(cumulative_ms(i) x 60 / 1000), so a 30 ms frame becomes 2, 2, 2,
    2, 1 ticks with no build-up. frame_count = round(duration x 60).
  * Delays of 0 and 10 ms are read as 100 ms, as most browsers do
    (PROVISIONAL, OD-06 option (a), owner Jolyn).
  * More than 1023 frames is refused: frame_count is 10 bits on the wire.
  * Each frame is centre-cropped to the layout's aspect ratio (cols : rows),
    resized to (cols x 8) by (rows x 8) and cut into one 8 x 8 region per
    Node ID from the layout and Node ID map (build step 4: position k is row
    k // cols, column k % cols over rows x cols; 0 = an empty cell). Step 4d
    (DS-15, 12 Nov 2026): rows and cols are 1 to 9 each, so up to 81 cells,
    with up to 9 tiles in any of them; the GIF is cut over the whole grid
    and only the placed tiles get their section. The map has rows x cols
    entries; the 9-entry form of the bench tools is still taken for a grid
    of fewer than 9 cells when the entries past the grid are 0.

T-B1-03 (pack and manifest):
  * Each channel value v becomes round(v x b / 255) on the PC only, where b
    is the brightness cap, 1 to 8; values above 8 are refused (PROVISIONAL,
    OD-02 option (b), owners Jolyn and Jia Jun). The cap is baked into the
    asset, so a brightness change means rebuilding and re-uploading every
    tile (VC-6).
  * 4 bits per channel, nibble order per OD-05 (``proto_layout.pack_frame``):
    96 B per frame, payload = frame_count x 96 B; CRC32 (zlib) over it.
  * Manifest {session_id, format_version 2, frame_count, crc32}; frame_rate
    60 is not on the wire (layout_table.md MANIFEST).

Usage:
    python3 pc/asset_builder.py in.gif --layout 2x2 --map 1,2,3,4,0,0,0,0,0
        [--brightness 8] [--session 1] --out assets/
"""

from __future__ import annotations

import argparse
import json
import os
import sys
import zlib
from dataclasses import dataclass
from typing import Dict, List, Sequence, Tuple

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import proto_layout as pl  # noqa: E402

#: Ticks per second (W6D-1a).
FPS = pl.FRAME_RATE

#: Largest frame_count (10 bits, VB-4b).
FRAME_LIMIT = pl.FRAME_MAX

#: GIF delays read as 100 ms (OD-06 option (a), PROVISIONAL).
SHORT_DELAYS_MS = (0, 10)
SHORT_DELAY_AS_MS = 100

#: Brightness cap: largest channel value after scaling (VC-6, OD-02).
BRIGHTNESS_MAX = pl.NIBBLE_MAX

#: Shipped asset format (layout_table.md section 5.4).
FORMAT_PACKED = 2

#: 8-bit format of the DE_PROFILE build only (OD-15): 192 B frames, one byte
#: per channel, values already scaled to 0 to 8 here. The shipped firmware
#: NACKs it (VC-5); it exists for the T-X-03 before/after timing run.
FORMAT_8BIT = 1

#: Tile side in LEDs.
TILE = 8

#: One RGB frame of a tile: 64 (r, g, b) tuples, LED 8 x row + column.
Pixels = List[Tuple[int, int, int]]


class AssetError(ValueError):
    """An input the plan rows refuse."""


@dataclass
class TileAsset:
    """One tile's packed asset and manifest values."""

    node_id: int
    payload: bytes
    frame_count: int
    crc32: int


def effective_delay(delay_ms: int) -> int:
    """OD-06 option (a): 0 and 10 ms become 100 ms; others unchanged."""
    return SHORT_DELAY_AS_MS if delay_ms in SHORT_DELAYS_MS else delay_ms


def check_frame_limit(frame_count: int) -> None:
    """Refuse an asset longer than the 10-bit frame_count allows."""
    if frame_count > FRAME_LIMIT:
        raise AssetError(
            "GIF needs %d frames at 60 fps; the limit is %d frames "
            "(frame_count is 10 bits, about 17 s)"
            % (frame_count, FRAME_LIMIT))
    if frame_count < 1:
        raise AssetError("GIF has no duration")


def resample_60fps(delays_ms: Sequence[int]) -> List[int]:
    """Source frame index for every 60 fps tick, by cumulative time.

    Source frame i ends at tick ceil(cumulative_ms(i) x 60 / 1000);
    frame_count = round(total_ms x 60 / 1000).
    """
    delays = [effective_delay(d) for d in delays_ms]
    total = sum(delays)
    frame_count = (total * FPS * 2 + 1000) // 2000  # round half up
    check_frame_limit(frame_count)
    ticks: List[int] = []
    cumulative = 0
    for index, delay in enumerate(delays):
        cumulative += delay
        end = -(-cumulative * FPS // 1000)  # ceil
        while len(ticks) < min(end, frame_count):
            ticks.append(index)
    while len(ticks) < frame_count:
        ticks.append(len(delays) - 1)
    return ticks


def tick_pattern(ticks: Sequence[int]) -> List[int]:
    """Number of ticks each source frame is shown, in order."""
    counts: List[int] = []
    for index, source in enumerate(ticks):
        if index == 0 or source != ticks[index - 1]:
            counts.append(0)
        counts[-1] += 1
    return counts


def load_gif(path: str):  # type: ignore[no-untyped-def]
    """Frames (RGB PIL images) and delays (ms) of a GIF."""
    from PIL import Image, ImageSequence  # Imported here: tests without GIFs
    frames = []
    delays = []
    with Image.open(path) as gif:
        for frame in ImageSequence.Iterator(gif):
            frames.append(frame.convert("RGB"))
            delays.append(int(frame.info.get("duration", 0)))
    return frames, delays


def parse_shape(layout: str) -> Tuple[int, int]:
    """'RxC' to (rows, cols): each 1 to 9, so up to 81 cells (layout_table
    section 5.2, step 4d)."""
    parts = layout.lower().split("x")
    if len(parts) != 2 or not all(part.isdigit() for part in parts):
        raise AssetError("layout must be RxC, for example 2x2 or 1x9")
    rows, cols = int(parts[0]), int(parts[1])
    if not (1 <= rows <= pl.GRID_MAX and 1 <= cols <= pl.GRID_MAX):
        raise AssetError("rows and cols are 1 to 9")
    return rows, cols


def normalise_map(layout: str, node_map: Sequence[int]) -> List[int]:
    """The map as rows x cols row-major cells. A 9-entry map for a grid of
    fewer cells (the bench tools' form) is cut to the grid when the entries
    past it are 0. Checks every rule; raises AssetError."""
    rows, cols = parse_shape(layout)
    cells = rows * cols
    entries = list(node_map)
    if len(entries) == pl.NODE_MAX and cells < pl.NODE_MAX:
        for slot, node in enumerate(entries[cells:], start=cells):
            if node:
                raise AssetError("position %d is outside the %s layout"
                                 % (slot, layout))
        entries = entries[:cells]
    if len(entries) != cells:
        raise AssetError("the %s map has %d cells, not %d"
                         % (layout, cells, len(entries)))
    used = []
    for node in entries:
        if not 0 <= node <= pl.NODE_MAX:
            raise AssetError("Node IDs are 1 to 9, 0 for an empty cell")
        if node:
            used.append(node)
    if len(used) != len(set(used)):
        raise AssetError("a Node ID appears twice in the map")
    if not used:
        raise AssetError("the map has no Node ID")
    return entries


def parse_layout(layout: str, node_map: Sequence[int]) -> Tuple[int, int]:
    """Check the layout and map; returns (rows, cols)."""
    normalise_map(layout, node_map)
    return parse_shape(layout)


def centre_crop(frame, rows: int, cols: int):  # type: ignore[no-untyped-def]
    """Largest box with the wall's aspect ratio (cols : rows), centred."""
    width, height = frame.size
    if width * rows > height * cols:  # Too wide: trim the sides.
        new_w = max(1, height * cols // rows)
        left = (width - new_w) // 2
        box = (left, 0, left + new_w, height)
    else:  # Too tall (or exact): trim top and bottom.
        new_h = max(1, width * rows // cols)
        top = (height - new_h) // 2
        box = (0, top, width, top + new_h)
    return frame.crop(box)


def partition_tiles(frames, layout: str,  # type: ignore[no-untyped-def]
                    node_map: Sequence[int]) -> Dict[int, List[Pixels]]:
    """Centre-crop and resize each frame to the wall, then cut one 8x8
    region per Node ID (row-major over rows x cols)."""
    from PIL import Image
    rows, cols = parse_layout(layout, node_map)
    node_map = normalise_map(layout, node_map)
    tiles: Dict[int, List[Pixels]] = {n: [] for n in node_map if n}
    for frame in frames:
        wall = centre_crop(frame, rows, cols).resize(
            (cols * TILE, rows * TILE), Image.BOX)
        data = wall.load()
        for slot, node in enumerate(node_map):
            if not node:
                continue
            row, col = divmod(slot, cols)
            pixels = [data[col * TILE + x, row * TILE + y]
                      for y in range(TILE) for x in range(TILE)]
            tiles[node].append(pixels)
    return tiles


def check_brightness(brightness: int) -> None:
    """OD-02 option (b): the cap is 1 to 8 (8/255 at most, VC-6)."""
    if not 1 <= brightness <= BRIGHTNESS_MAX:
        raise AssetError("brightness must be 1 to %d (the 8/255 cap, VC-6)"
                         % BRIGHTNESS_MAX)


def scale(value: int, brightness: int = BRIGHTNESS_MAX) -> int:
    """round(v x brightness / 255), half up (no ties occur for b = 8)."""
    return (value * brightness * 2 + 255) // 510


def pack_frame(pixels: Pixels, brightness: int = BRIGHTNESS_MAX) -> bytes:
    """One 96 B packed frame from 64 RGB pixels (OD-05 nibble order)."""
    check_brightness(brightness)
    if len(pixels) != TILE * TILE:
        raise AssetError("a tile frame has 64 pixels")
    values = [scale(channel, brightness)
              for pixel in pixels for channel in pixel]
    return pl.pack_frame(values)


def build_tile(node_id: int, frames: Sequence[Pixels],
               brightness: int = BRIGHTNESS_MAX) -> TileAsset:
    """Packed payload and CRC32 of one tile."""
    check_frame_limit(len(frames))
    payload = b"".join(pack_frame(p, brightness) for p in frames)
    return TileAsset(node_id, payload, len(frames), zlib.crc32(payload))


def build_tile_8bit(node_id: int, frames: Sequence[Pixels],
                    brightness: int = BRIGHTNESS_MAX) -> TileAsset:
    """OD-15 8-bit asset: 192 B per frame, each byte round(v x b / 255)."""
    check_brightness(brightness)
    check_frame_limit(len(frames))
    payload = bytes(scale(channel, brightness) for pixels in frames
                    for pixel in pixels for channel in pixel)
    return TileAsset(node_id, payload, len(frames), zlib.crc32(payload))


def build_manifest(session_id: int, seq: int, asset: TileAsset) -> bytes:
    """Encoded MANIFEST for one tile."""
    return pl.encode_manifest(session_id=session_id, seq=seq,
                              format_version=FORMAT_PACKED,
                              frame_count=asset.frame_count,
                              crc32=asset.crc32)


def build_assets(frames, delays: Sequence[int], layout: str,  # type: ignore
                 node_map: Sequence[int], brightness: int = BRIGHTNESS_MAX,
                 eight_bit: bool = False) -> Dict[int, TileAsset]:
    """GIF frames and delays to one TileAsset per Node ID (packed, or the
    OD-15 8-bit test format)."""
    check_brightness(brightness)
    ticks = resample_60fps(delays)
    tiles = partition_tiles(frames, layout, node_map)
    build = build_tile_8bit if eight_bit else build_tile
    return {node: build(node, [regions[i] for i in ticks], brightness)
            for node, regions in tiles.items()}


def write_assets(assets: Dict[int, TileAsset], out_dir: str, layout: str,
                 node_map: Sequence[int], session_id: int,
                 brightness: int) -> str:
    """node_<n>.bin per tile plus session.json; returns the json path."""
    os.makedirs(out_dir, exist_ok=True)
    tiles = {}
    for node, asset in sorted(assets.items()):
        name = "node_%d.bin" % node
        with open(os.path.join(out_dir, name), "wb") as handle:
            handle.write(asset.payload)
        tiles[str(node)] = {"file": name, "frame_count": asset.frame_count,
                            "crc32": asset.crc32}
    info = {"layout": layout, "node_map": list(node_map),
            "session_id": session_id, "brightness": brightness,
            "format_version": FORMAT_PACKED, "tiles": tiles}
    path = os.path.join(out_dir, "session.json")
    with open(path, "w", encoding="utf-8") as handle:
        json.dump(info, handle, indent=2)
    return path


def main(argv: List[str] = None) -> int:  # type: ignore[assignment]
    """Command line entry point; exit 1 with the reason on a refusal."""
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("gif")
    parser.add_argument("--layout", default="1x1",
                        help="RxC, rows and cols 1 to 9, rows x cols <= 9")
    parser.add_argument("--map", default="1,0,0,0,0,0,0,0,0",
                        help="9 Node IDs, row by row over rows x cols, "
                             "0 = blank")
    parser.add_argument("--brightness", type=int, default=BRIGHTNESS_MAX)
    parser.add_argument("--session", type=int, default=1)
    parser.add_argument("--out", default="assets")
    parser.add_argument("--format", default="packed",
                        choices=("packed", "8bit"),
                        help="8bit only for the DE_PROFILE test build (OD-15)")
    args = parser.parse_args(argv)
    try:
        node_map = [int(n) for n in args.map.split(",")]
        frames, delays = load_gif(args.gif)
        assets = build_assets(frames, delays, args.layout, node_map,
                              args.brightness, args.format == "8bit")
    except (AssetError, ValueError) as err:
        print("asset_builder: %s" % err, file=sys.stderr)
        return 1
    path = write_assets(assets, args.out, args.layout, node_map,
                        args.session, args.brightness)
    for node, asset in sorted(assets.items()):
        print("node %d: %d frames, %d B, crc32 0x%08x" % (
            node, asset.frame_count, len(asset.payload), asset.crc32))
    print("wrote %s" % path)
    return 0


if __name__ == "__main__":
    sys.exit(main())
