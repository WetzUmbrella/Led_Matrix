# Power and heating, 30 min at full white (T-B5-08)

Owner: Jia Jun (Buddy 5). Plan row T-B5-08: "30 min at full white with every LED byte at the cap value 8, tile on its USB-C battery pack; current measured with a USB meter; board checked for whether the matrix is fed from VBUS or the 800 mA 3.3 V regulator. Temperature of the board (next to the LED matrix and at the ESP32-S3 module) and of the battery pack measured with a contact or infrared thermometer at 0, 15 and 30 min." Pass: "0 brownout resets in 30 min (reset-reason log); measured current recorded beside the 0.12 A estimate; all six temperature readings at or below the limits set in OD-22. Pass = 3 of 3." Lecturer feedback (`SE15 (1).txt`): "cap brightness and check the USB-C pack and board heating over 30 minutes." Nothing has run yet.

**Blocked on OD-22** (owner Jia Jun): the temperature limits. Options from the plan: limits from the battery pack's datasheet and the ESP32-S3 module's operating range. Until OD-22 is settled the temperatures are recorded but the third pass condition cannot be judged.

## Set-up

1. Full-white asset at the cap: a GIF of one white frame (`asset_builder.py` scales 255 to 8 on every channel; check with `pc/tests/check_assets.py`-style decoding that every nibble is 8). Upload and play it in a loop (10 s asset, repeating).
2. Power path: tile on its USB-C pack through the USB meter. Record from the board schematic (Waveshare ESP32-S3-Matrix wiki) whether the WS2812B matrix is fed from VBUS (5 V) or the 3.3 V regulator; write the source here: `[NEEDS CHECK]`.
3. Thermometer points: P1 board next to the LED matrix, P2 the ESP32-S3 module shield, P3 the battery pack surface.

## Log

| Time | Current (A) | P1 board (C) | P2 module (C) | P3 pack (C) | Reset reason / notes |
|---|---|---|---|---|---|
| 0 min | | | | | |
| 15 min | | | | | |
| 30 min | | | | | |

Brownouts: count `rst:` lines in the tile log other than the first boot (`idf.py monitor | tee docs/week13/power_thermal_tile.log`); a brownout shows `rst:0xf (BROWNOUT_RST)`. Estimate for comparison: 0.12 A (plan row). Photos of the meter and thermometer at each time go beside this file.
