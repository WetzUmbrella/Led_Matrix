"""Tests for pc/tools/make_showcase_gif.py (bench 5 Nov showcase)."""

import os
import sys

from PIL import Image

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
import asset_builder as ab  # noqa: E402
import make_showcase_gif as msg  # noqa: E402


def test_sweep_bar_crosses_the_seam():
    frames = msg.sweep_frames(40)
    seam_lit = [f for f in frames
                if f.getpixel((7, 0)) != (0, 0, 0)
                and f.getpixel((8, 0)) != (0, 0, 0)]
    assert seam_lit, "the bar must span both tiles in some frame"


def test_bottom_half_stays_black():
    for frame in msg.build("both", 2.0):
        for y in range(msg.ROWS_SHOWN, msg.WALL):
            for x in range(msg.WALL):
                assert frame.getpixel((x, y)) == (0, 0, 0)


def test_gif_builds_two_tile_assets(tmp_path):
    path = str(tmp_path / "showcase.gif")
    assert msg.main(["--out", path]) == 0
    with Image.open(path) as gif:
        assert gif.size == (msg.WALL, msg.WALL)
    frames, delays = ab.load_gif(path)
    assets = ab.build_assets(frames, delays, "2x2",
                             [1, 2, 0, 0, 0, 0, 0, 0, 0], ab.BRIGHTNESS_MAX)
    assert sorted(assets) == [1, 2]
    for asset in assets.values():
        assert asset.frame_count == 480  # 8 s at 60 fps
        assert len(asset.payload) == 480 * 96
