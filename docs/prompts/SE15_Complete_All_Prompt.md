# SE15 Complete-All Prompt: verify progress, then build every module

Paste everything below the line into a fresh Claude Code session in WSL, opened in `~/Led_Matrix`. Mount the Drive first if WSL has restarted: `sudo mount -t drvfs G: /mnt/g`.

---

## Role

You are the lead engineer for team SE15 (Wireless Modular LED Video Wall, INF2004 Embedded Systems, SIT), working for Ahsanul (Buddy 4). You have two jobs:

1. Verify exactly where the project stands.
2. Complete every subsystem's firmware and PC code to the implementation plan, the Tribunal 1 commitments and the Barr-C coding standard.

Ahsanul commits all of it. The other buddies will review and defend their own subsystems, so every module you write must be explainable by its owner.

Think like the examiner. Every claim ties to timing, memory, concurrency, interfaces or hardware limits, with numbers where numbers exist.

## Where things live

- **Repo:** `~/Led_Matrix` (WSL). This is the only working copy. Never build in or write to the Drive folder.
- **Project documents (read only):** `/mnt/g/Other computers/My Computer/SIT/Year 2 Tri 1/INF2004 - Embedded Systems/Project/`. If the path is missing, ask me to remount it; don't search the disk. Read `.docx` files with `pandoc -t plain --wrap=none`, in full, in chunks.
- **ESP-IDF:** run `. ~/esp/esp-idf/export.sh` in every new shell before `idf.py`. ESP-IDF v5.3 on target `esp32s3`; Pico SDK 2.3.1, as in CI.

## Binding inputs, in priority order

1. `SE15_PostTri1.docx`: the lecturer said this **must** be implemented. Every row of the change log, shared resource matrix, interface map, risks, test plan and Version C fixes is a commitment.
2. `SE15 (1).txt`: lecturer feedback on Week 6.
3. `Plan/SE15_Implementation_Plan.md` (tasks T-B*/T-X, open decisions OD-01 to OD-24), `Plan/commitment_inventory.md`, `Plan/rubric_matrix.md`, `Plan/critic_log.md` (rulings, treat as settled).
4. `SE15-W6/inf2004_SE15_W6.pdf`: FR, NFR and A IDs, the task table and the statecharts. View the statechart images directly; don't rely on PDF text extraction for diagrams.
5. `Week10_Kickoff/` (`decision_briefs.md`, `bench_checklist.md`, `review_log.md`).
6. In the repo:
   - `docs/coding_standard.md` (the Barr-C rules and the approved deviations D-1 and D-2);
   - `tools/barr_check.py` and `.clang-format`;
   - `docs/templates/`;
   - `docs/b4/` (status, dependencies, review log);
   - `panel_sync/main/timesync_DEFENCE.md`.
7. The Barr-C:2018 PDF (`https://barrgroup.com/sites/default/files/barr_c_coding_standard_2018.pdf`). Cite only rule numbers checked against it. The repo is public and the PDF is for internal use only, so describe rules in your own words.

If an input is missing, say so and carry on. Never fill a gap from memory.

## Git: commits are Ahsanul's only

- **Identity:** commit with the configured identity, `WetzUmbrella <ahsanulpro@gmail.com>`. Check it before the first commit; if it differs, stop and ask.
- **No Claude attribution, anywhere:** no `Co-Authored-By`, no session links, no "Generated with" lines in commits, tags or PR text. A past trailer forced a whole repo move, so this rule is absolute.
- **Check the hook first.** `.git/hooks/commit-msg` must exist and reject such messages. Verify it with a throwaway `--allow-empty` commit that must fail with exit 1; if it doesn't fail, stop. Also confirm `~/.claude/settings.json` has `"includeCoAuthoredBy": false`.
- **Check every commit:** after each one, run `git log -1 --format='%an <%ae>%n%B'`.
- **Branches:** one branch per plan task, `b<n>/<task-id>-<short-name>` (for example `b3/t-b3-03-tile-net`), based on the latest merged work it depends on. Write small commits whose messages cite the task ID.
- **No pushing:** never push to `master`, never force-push, never rewrite pushed history, never move `cr1-before`. Push branches or open PRs only when I say so, and in batches I approve.

## Hard rules

- **PostTri constants and behaviour are frozen.** Change one only with my approval, logging the PostTri row it touches.
- **Barr-C is mandatory** for all team C code, through the existing standard, checker and templates:
  - extend `tools/barr_scope.txt` to each new file as it lands;
  - the only deviations are D-1 and D-2, and any new one needs my approval first;
  - Python on the PC side follows PEP 8 with type hints, and has pytest for every behaviour in its plan row.
- **Interfaces:** follow the plan's module, file and function names, with the Barr-C module prefix (for example `tile_net_task`, `display_engine_start`). Record each rename in that module's DEFENCE doc.
- **Wire format:** explicit shifts and masks, little-endian, from one layout table. No packed structs and no bit-fields on the wire.
- **Open decisions owned by others** (OD-xx):
  - if a brief in `Week10_Kickoff/decision_briefs.md` gives a recommendation, implement it and mark it `PROVISIONAL (OD-xx, owner <name>)`;
  - if the decision is settled by measurement, make it a compile-time or config switch;
  - list every provisional choice for the owner to confirm. Never mark an OD settled.
- **No invented evidence.** You can't touch a tile, the Pico or a logic analyser. Every measured value becomes a bench procedure with exact commands, the pass criterion copied from the plan row and the evidence file name, added to `bench_checklist.md` in run order. Mark unmeasured values `[NEEDS MEASUREMENT]`.
- **Ownership stays visible.** Every module file's header names its owner and says `Implemented by Ahsanul (B4) for <owner>, task T-xx`. Each subsystem gets `docs/<module>_DEFENCE.md` in the style of `timesync_DEFENCE.md`:
  - each design choice, the PostTri row it comes from, and the examiner's likely question;
  - a "Names" paragraph mapping plan names to Barr-C names;
  - three questions the owner must answer in their own words before Tribunal 2, with no answers given.
- **Prototype code** (`render.c`, `frames.c`, the packed structs, the old loops) is deleted in the same commit that lands its replacement, as plan section 0.1 says. Never leave two paths for the same job.
- **Text:** British English, metric units, no em dashes.

## Phase 0: Verify progress (read only), then STOP

Fetch everything (`git fetch --all --tags`). List every local and remote branch, with whether it's merged and how far it is ahead of `master`.

Write `docs/status/overall.md`:

1. **Every plan task (T-B1 to T-B5, T-X):** status (Done, Partial or Not started), evidence (`path:line` or commit), unmet dependencies, and whether it passes `barr_check`.
2. **Every PostTri commitment:** the implementing task, its state in code, and any contradiction.
3. **Every `SE15 (1).txt` bullet:** what closes it.
4. **Every OD:** settled, provisional or open, and the tasks it blocks.
5. **Bench evidence** that exists against what the plan needs.
6. **The critical path to Week 10 and to Week 13**, with today's date and the deadlines. Ask me for the dates if you can't confirm them.

Run every test that exists today (host tests, pytest, `barr_check`, `clang-format --dry-run`, and both firmware builds) and paste the results.

**STOP.** Show me the summary and the proposed build order (below), adjusted for what you found. Wait for my go-ahead.

## Phase 1: Shared foundation

Build these first, because everything else depends on them:

1. **T-B2-01 `common/proto`:** the full layout table in `protocol/layout_table.md`, `proto_layout.h/.c` with one encode/decode pair per message, and `test_proto.c` with the boundary set from the row. This is an ESP-IDF component for the tiles and a source file for the Pico build.
2. **T-B1-01 `pc/proto_layout.py`:** the Python twin, plus `gen_vectors.py` and `protocol/vectors/`. Cross-language tests must show 0 mismatches.
3. **T-B5-04 `tile_tasks.h`** and the `sdkconfig.defaults` options from the row.
4. **T-B3-06 `fault_word.c/.h`:** atomic set, clear only on ACK, `EVENT_MASK`.

## Phase 2: Week 10 path, in plan order

Build and wire these in, then delete the matching prototype paths:

1. T-B3-02 and T-B3-01: the flash check procedure and the heap probe.
2. T-B5-01: `display_engine` gptimer one-shot tick, the test pin, `DE_W10_TEST_PATTERN` with the 1 ms / 4 ms marker from `RIG.md`.
3. T-B3-03: `tile_net`, with the OD-09 hook switch, Time Sync queue length 4 and Command queue length 3 (both PROVISIONAL).
4. Wire in timesync (T-B4-02); remove `lock` and `get_render_state`.
5. T-B2-02: Pico `coordinator_loop` in poll mode, absolute 100 ms beacon, `t_p` read immediately before `udp_sendto`, a lateness histogram. OD-01 is PROVISIONAL until Benjamin adds the SDK citation.
6. T-B4-03: the rig build is ready. Add the bench steps to `bench_checklist.md` Part B.

**Gate:** both firmware builds pass, all host tests and pytest pass, and `barr_check` reports 0 findings. Show me the Week 10 summary before continuing.

## Phase 3: Week 13, in dependency order (plan section 7 graph)

- **Subsystem 1, Jolyn (PC):** T-B1-02, T-B1-03, T-B1-04, T-B1-05, T-B1-06.
- **Subsystem 2, Benjamin (Pico):** T-B2-03, T-B2-04, T-B2-05, T-B2-06 (the simulated nine-node load).
- **Subsystem 3, Cheston (tile comms and storage):** T-B3-04, T-B3-05.
- **Subsystem 4, Ahsanul (sync and playback):** T-B4-04, with the OD-10 rule already decided: window full, drift available, and the newest sample no older than the 1 s holdover.
- **Subsystem 5, Jia Jun (display and orientation):** T-B5-02, T-B5-03, T-B5-05.
- **Cross-subsystem test builds and tools:** T-X-01 to T-X-08 (stress builds, profiling builds and analysis scripts), plus the procedures for every hardware test.
- **T-B4-07** (the two-way fallback): only if a skew result I supply exceeds 5 ms.
- **T-X-06 and T-B1-07:** generate the evidence documents from what exists, marking missing artefacts "not yet produced".

For each task:

- implement it exactly as its row says;
- add host tests or pytest for every testable behaviour in the row's acceptance test;
- delete the prototype path it replaces;
- update that module's DEFENCE doc and `docs/status/overall.md`.

## Subagents

Use one implementer subagent per subsystem when tasks have no dependency between them. Give each the binding inputs, the standard, its plan rows and the current interfaces. Each implementer works on its own branch and returns a summary; you merge in dependency order and run the gates.

Then run two reviewers with fresh context, after each phase. They return findings only, each tagged BLOCKING, SHOULD FIX or NIT with `path:line` and a fix:

- **Tribunal fidelity examiner:**
  - the code matches the PostTri and plan rows (constants, ordering, ownership of shared data, queue policies, state names from the W6 statecharts);
  - every module survives one follow-up question from the lecturer;
  - no evidence is claimed that doesn't exist.
- **Barr-C and integration auditor:**
  - 0 checker findings and no undeclared deviations;
  - interfaces agree across modules and languages;
  - no prototype path survives beside its replacement;
  - every commit is authored correctly with no trailer.

**Loop:** fix and resubmit until both reviewers return 0 BLOCKING and at most 3 SHOULD FIX, or after 3 rounds. Log each round in `docs/status/review_log.md`.

## Reply after each phase

Keep it short:

- branches and commits;
- test and build results;
- provisional decisions for each owner to confirm;
- new bench steps for me, in run order;
- a draft message per buddy saying what was built for their subsystem, where it is, what they must confirm, and their DEFENCE doc questions;
- open risks.

Don't recap the steps.
