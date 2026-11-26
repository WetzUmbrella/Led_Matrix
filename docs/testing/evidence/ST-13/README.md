# ST-13: Tile unplugged mid-round

**Requirements.** FR20 (a lost tile is skipped and rejoins)

**Procedure.** During an easy round, unplug tile 9 (14:57:55) and plug it back (14:58:10).

**Pass rule.** The round goes on; the tile takes part again when it is back.

**Result.** The Pico marked tile 9 lost at 14:57:58.4 and left it out of the mole choice; the round ran to the end (30 of 30 moles). Tile 9 was back at 14:58:10.1 but showed no hole until the results. The round command goes to each tile once, at the start, so a tile that restarts never learns the round is running, while the Pico puts it back in the mole choice. **Verdict: PARTLY (open).**

**Files.** The logs are in [`../ST-14/`](../ST-14/).
