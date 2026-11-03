"""pytest: pc/asset_builder.py (T-B1-02 resample, T-B1-03 pack).

Owner: Jolyn (Buddy 1). Implemented by Ahsanul (B4) for Jolyn.
"""

from __future__ import annotations

import os
import sys
import zlib

import pytest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
import asset_builder as ab  # noqa: E402
import proto_layout as pl  # noqa: E402

VECTORS = os.path.join(os.path.dirname(__file__), "..", "..", "protocol",
                       "vectors")


def test_30ms_frames_give_2_2_2_2_1() -> None:
    ticks = ab.resample_60fps([30] * 50)
    assert ab.tick_pattern(ticks)[:10] == [2, 2, 2, 2, 1] * 2


def test_10s_gif_gives_600_frames() -> None:
    assert len(ab.resample_60fps([100] * 100)) == 600
    assert len(ab.resample_60fps([40] * 250)) == 600


def test_18s_gif_refused_with_limit_message(tmp_path, capsys) -> None:
    with pytest.raises(ab.AssetError, match="1023"):
        ab.resample_60fps([100] * 180)
    from PIL import Image
    path = str(tmp_path / "long.gif")
    frames = [Image.new("RGB", (8, 8), (i, 0, 0)) for i in range(180)]
    frames[0].save(path, save_all=True, append_images=frames[1:],
                   duration=100, loop=0)
    assert ab.main([path, "--out", str(tmp_path / "out")]) == 1
    assert "1023" in capsys.readouterr().err


def test_no_build_up_after_600_frames() -> None:
    delays = [30, 20, 40, 20, 30] * 120  # 600 frames, 16.8 s
    ticks = ab.resample_60fps(delays)
    cumulative = 0
    for index, delay in enumerate(delays):
        cumulative += delay
        shown = sum(1 for t in ticks if t <= index)
        assert abs(shown - cumulative * 60 / 1000) < 1  # never drifts
    assert len(ticks) == round(cumulative * 60 / 1000)


@pytest.mark.parametrize("delay,expected", [(0, 100), (10, 100), (20, 20),
                                            (100, 100)])
def test_od06_short_delays(delay: int, expected: int) -> None:
    assert ab.effective_delay(delay) == expected


def _solid(rgb):  # type: ignore[no-untyped-def]
    return [rgb] * 64


def test_600_frame_asset_is_57600_bytes_with_zlib_crc() -> None:
    asset = ab.build_tile(1, [_solid((255, 128, 0))] * 600)
    assert len(asset.payload) == 57600
    assert asset.crc32 == zlib.crc32(asset.payload)
    manifest = pl.decode_manifest(ab.build_manifest(3, 1, asset))
    assert manifest["crc32"] == asset.crc32
    assert manifest["frame_count"] == 600
    assert manifest["format_version"] == 2


def test_every_nibble_at_most_8() -> None:
    frame = ab.pack_frame([(v, 255 - v, v // 2) for v in range(0, 256, 4)])
    assert max(pl.unpack_frame(frame)) <= 8


def test_t_b1_01_vector_reproduced() -> None:
    with open(os.path.join(VECTORS, "frame_od05_worked.bin"), "rb") as fh:
        expected = fh.read()
    # Channel values that scale to 1..6: round(v x 8 / 255) = n for v = 32n.
    pixels = [(32, 64, 96), (128, 160, 191)] + [(0, 0, 0)] * 62
    assert ab.pack_frame(pixels) == expected


@pytest.mark.parametrize("value", [0, 9, 40, 255])
def test_brightness_above_8_refused(value: int) -> None:
    with pytest.raises(ab.AssetError):
        ab.check_brightness(value)


def test_partition_2x2_cuts_four_regions() -> None:
    from PIL import Image
    wall = Image.new("RGB", (16, 16))
    for slot, colour in enumerate([(255, 0, 0), (0, 255, 0), (0, 0, 255),
                                   (255, 255, 255)]):
        row, col = divmod(slot, 2)
        for y in range(8):
            for x in range(8):
                wall.putpixel((col * 8 + x, row * 8 + y), colour)
    tiles = ab.partition_tiles([wall], "2x2", [1, 2, 0, 3, 4, 0, 0, 0, 0])
    assert tiles[1][0][0] == (255, 0, 0)
    assert tiles[2][0][0] == (0, 255, 0)
    assert tiles[3][0][0] == (0, 0, 255)
    assert tiles[4][0][63] == (255, 255, 255)


def test_bad_maps_refused() -> None:
    with pytest.raises(ab.AssetError):
        ab.parse_layout("2x2", [1, 2, 3, 0, 0, 0, 0, 0, 0])  # column 2
    with pytest.raises(ab.AssetError):
        ab.parse_layout("1x1", [1, 1, 0, 0, 0, 0, 0, 0, 0])
    with pytest.raises(ab.AssetError):
        ab.parse_layout("4x4", [1] + [0] * 8)
