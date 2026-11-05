"""Tests for pc/tools/make_character_gif.py (bench 5 Nov showcase)."""

import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
import asset_builder as ab  # noqa: E402
import make_character_gif as mcg  # noqa: E402


def test_blip_crosses_the_vertical_seam():
    frames = mcg.build()
    crossing = [f for f in frames
                if f.getpixel((7, 8)) == mcg.BODY
                and f.getpixel((8, 8)) == mcg.BODY]
    assert crossing, "Blip must span tiles 1 and 2 in some frame"


def test_ground_line_on_every_frame():
    for frame in mcg.build():
        assert frame.getpixel((0, mcg.GROUND_ROW)) == mcg.GROUND


def test_gif_builds_2x2_assets(tmp_path):
    path = str(tmp_path / "blip.gif")
    assert mcg.main(["--out", path]) == 0
    frames, delays = ab.load_gif(path)
    assets = ab.build_assets(frames, delays, "2x2",
                             [3, 5, 0, 4, 2, 0, 0, 0, 0], ab.BRIGHTNESS_MAX)
    assert sorted(assets) == [2, 3, 4, 5]
    for asset in assets.values():
        assert asset.frame_count == 300  # 5 s at 60 fps
