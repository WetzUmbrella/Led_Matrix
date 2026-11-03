"""pytest: T-B1-06 numbered-pattern assets for 1x1, 2x2 and 3x3 (14 tile
assets, PC only, VC-11).

Owner: Jolyn (Buddy 1). Implemented by Ahsanul (B4) for Jolyn.
"""

from __future__ import annotations

import os
import sys

sys.path.insert(0, os.path.dirname(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
import asset_builder as ab  # noqa: E402
import check_assets  # noqa: E402
import numbered_pattern as npat  # noqa: E402


def test_fourteen_tile_assets_pass() -> None:
    passed = 0
    for layout, node_map in npat.DEFAULT_MAPS.items():
        frames, delays = npat.make_frames(layout, node_map)
        for asset in ab.build_assets(frames, delays, layout,
                                     node_map).values():
            assert check_asset_ok(asset)
            passed += 1
    assert passed == 14


def check_asset_ok(asset: ab.TileAsset) -> bool:
    return check_assets.check_asset(asset) == []
