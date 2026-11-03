#!/usr/bin/env python3
"""Build the Tribunal 2 evidence index (T-B1-07).

Owner: Jolyn (Buddy 1), task T-B1-07 (all buddies support). Implemented by
Ahsanul (B4) for Jolyn, written 30 Oct 2026 in Jia Jun's (B5) session.

Reads the commitment inventory and the implementation plan from the
hand-off ``inputs/`` folder (kept outside the repository on purpose, so only
IDs, task IDs, owners and artefact links are copied in, never the commitment
text) and writes ``docs/tribunal2/evidence_index.md``:
  * one row per in-scope inventory ID, grouped by the inventory sections
    (the Week 8 template sections): plan tasks, owner, where the work is
    (hand-off patch or branch), the evidence artefact from the plan row, and
    the status from integration evidence: "artefact present" only when the
    artefact file exists in the repository, otherwise "not yet produced";
  * the Week 10 readiness rows re-rated, Green only where an artefact
    exists;
  * a per-buddy list of owned tasks and their artefacts;
  * the requirement to task to test trace (FR/NFR IDs named in each plan
    row, the row's acceptance test and its result so far);
  * the AI-use notes table for the owners to complete.
Pass check printed at the end (plan row): 0 in-scope IDs without a row,
each buddy linked to at least one artefact, 0 uncited Green rows.

Usage:
    python3 pc/tools/evidence_index.py --inputs ../inputs
"""

from __future__ import annotations

import argparse
import os
import re
import sys
from dataclasses import dataclass, field
from typing import Dict, List, Optional, Tuple

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
OUT = os.path.join(ROOT, "docs", "tribunal2", "evidence_index.md")

BUDDIES = {"Jolyn": "B1", "Benjamin": "B2", "Cheston": "B3",
           "Ahsanul": "B4", "Jia Jun": "B5"}

#: Where each task's work is (hand-off patches, or branches before them).
WORK = {
    "T-B4-01": "branch b4/t-b4-01-before-sizes", "T-B2-01": "patch 02",
    "T-B1-01": "branch b1/t-b1-01-proto-layout-py",
    "T-B5-04": "branch b5/t-b5-04-tile-tasks", "T-B3-06": "patch 09",
    "T-B5-01": "branch b4/wire-w10", "T-B3-03": "patch 04",
    "T-B3-01": "patch 04", "T-B3-02": "branch b4/phase1",
    "T-B4-02": "branch b4/timesync-barr", "T-B4-03": "branch "
    "b4/t-b4-03-bench-checklist", "T-B2-02": "patch 03",
    "T-B2-03": "patch 03", "T-B2-04": "patch 03", "T-B2-05": "patch 03",
    "T-B2-06": "patch 07", "T-B4-04": "patch 05", "T-B3-04": "patch 11",
    "T-B3-05": "patch 11", "T-B5-02": "patch 12", "T-B5-03": "patch 12",
    "T-B5-05": "patch 12", "T-B1-02": "patch 13", "T-B1-03": "patch 13",
    "T-B1-04": "patch 13", "T-B1-05": "patch 13", "T-B1-06": "patch 13",
    "T-X-01": "patch 14", "T-X-02": "patch 14", "T-X-03": "patch 14",
    "T-X-04": "patch 14", "T-X-05": "patch 14", "T-X-06": "patch 14",
    "T-X-08": "patch 14", "T-B5-07": "patch 14", "T-B1-07": "patch 14",
}

TASK_RE = re.compile(r"T-(?:B\d|X)-\d\d")
REQ_RE = re.compile(r"\b(N?FR\d+)\b")
PATH_RE = re.compile(r"`([\w./-]+\.\w+)`")


@dataclass
class Task:
    """One plan row."""

    task_id: str
    owner: str
    files: str
    artefact: str
    acceptance: str
    week: str
    reqs: List[str] = field(default_factory=list)


def cells(line: str) -> List[str]:
    """Cells of one markdown table row."""
    return [c.strip() for c in line.strip().strip("|").split("|")]


def load_plan(path: str) -> Dict[str, Task]:
    """Task rows of the implementation plan."""
    tasks: Dict[str, Task] = {}
    with open(path, encoding="utf-8") as handle:
        for line in handle:
            row = cells(line)
            if len(row) >= 10 and TASK_RE.fullmatch(row[0]):
                owner = row[1].split("(")[0].strip()
                tasks[row[0]] = Task(row[0], owner, row[4], row[8], row[7],
                                     row[9],
                                     sorted(set(REQ_RE.findall(line))))
    return tasks


def load_inventory(path: str) -> List[Tuple[str, str, List[str]]]:
    """(section, ID, tasks) for every in-scope inventory row."""
    rows = []
    section = ""
    with open(path, encoding="utf-8") as handle:
        for line in handle:
            if line.startswith("## "):
                section = line[3:].strip()
            row = cells(line)
            if not line.startswith("| ") or len(row) < 5:
                continue
            if row[0] in ("ID", "Item") or row[0].startswith("---"):
                continue
            if not re.fullmatch(r"[A-Z0-9]+-[0-9A-Za-z.]+", row[0]):
                continue
            if "In" not in row[2:-2] and "In" not in row:
                continue
            rows.append((section, row[0], TASK_RE.findall(row[-1])))
    return rows


#: A document holding this text is a procedure still waiting for its run,
#: not evidence (every procedure written before its run says so).
NOT_RUN = "Nothing has run yet"


def artefacts_present(text: str) -> List[str]:
    """Backticked paths in ``text`` that exist in the repository, except
    this index itself and procedures that have not run."""
    found = []
    for path in PATH_RE.findall(text):
        full = os.path.join(ROOT, path)
        if not os.path.isfile(full) or os.path.abspath(full) == OUT:
            continue
        if path.endswith(".md"):
            with open(full, encoding="utf-8") as handle:
                if NOT_RUN in handle.read():
                    continue
        found.append(path)
    return found


def status_of(task: Optional[Task]) -> Tuple[str, List[str]]:
    """Status from integration evidence and the artefacts present.

    Evidence: an artefact-column path, or a ``docs/`` path in the files
    column, that exists. Code: a non-docs path of the files column that
    exists (the work is there; its bench evidence may not be).
    """
    if task is None:
        return "no plan task", []
    named = artefacts_present(task.artefact + " " + task.files)
    evidence = [p for p in named if p.startswith("docs/")
                or p in artefacts_present(task.artefact)]
    code = [p for p in named if p not in evidence]
    if evidence:
        return "evidence present", evidence
    if code:
        return "code present, bench evidence not yet produced", code
    return "not yet produced", []


def build(inputs: str) -> Tuple[str, Dict[str, int]]:
    """The index text and the pass-check counts."""
    tasks = load_plan(os.path.join(inputs, "SE15_Implementation_Plan.md"))
    inventory = load_inventory(os.path.join(inputs,
                                            "commitment_inventory.md"))
    out = ["# Tribunal 2 evidence index (T-B1-07)", "",
           "Generated by `pc/tools/evidence_index.py` from the hand-off "
           "`inputs/` (commitment inventory and implementation plan). IDs "
           "only: the commitment text stays in the inputs. Status comes from "
           "integration evidence: \"artefact present\" only when the plan "
           "row's artefact file exists in this repository; otherwise \"not "
           "yet produced\". Work: the hand-off patch or branch that holds the "
           "task (nothing is committed on master yet).", ""]
    missing_rows = 0
    section = None
    for sec, item, item_tasks in inventory:
        if sec != section:
            section = sec
            out += ["## %s" % sec, "",
                    "| ID | Task(s) | Owner | Work | Artefact | Status |",
                    "|---|---|---|---|---|---|"]
        if not item_tasks:
            missing_rows += 1
            out.append("| %s | none in the plan | | | | no plan task |"
                       % item)
            continue
        owners, works, arts, states = [], [], [], []
        for task_id in item_tasks:
            task = tasks.get(task_id)
            state, present = status_of(task)
            owners.append(task.owner if task else "?")
            works.append(WORK.get(task_id, "not started"))
            arts.append(", ".join("`%s`" % p for p in present)
                        or "not yet produced")
            states.append(state)
        out.append("| %s | %s | %s | %s | %s | %s |" % (
            item, ", ".join(item_tasks), ", ".join(dict.fromkeys(owners)),
            "; ".join(dict.fromkeys(works)), "; ".join(dict.fromkeys(arts)),
            "; ".join(dict.fromkeys(states))))
        out.append("")
    out += readiness(inventory, tasks)
    per_buddy, linked = buddies(tasks)
    out += per_buddy
    out += trace(tasks)
    out += ai_notes()
    counts = {"inventory rows": len(inventory),
              "rows without a task": missing_rows,
              "buddies linked to an artefact": linked}
    out += ["## Pass check", "",
            "- In-scope inventory IDs: %d; rows: %d; IDs with no plan task: "
            "%d (each is listed with \"no plan task\")." % (
                len(inventory), len(inventory), missing_rows),
            "- Buddies linked to at least one artefact: %d of 5." % linked,
            "- Green readiness rows without an artefact: 0 (Green is given "
            "only when one exists).", ""]
    return "\n".join(out), counts


def readiness(inventory: List[Tuple[str, str, List[str]]],
              tasks: Dict[str, Task]) -> List[str]:
    """Week 10 readiness rows, Green only with an artefact."""
    out = ["## Readiness re-rated from evidence", "",
           "| ID | Task(s) | Rating | Cited artefact |", "|---|---|---|---|"]
    for sec, item, item_tasks in inventory:
        if not sec.startswith("12."):
            continue
        present: List[str] = []
        for task_id in item_tasks:
            state, paths = status_of(tasks.get(task_id))
            if state == "evidence present":
                present += paths
        rating = "Green" if present else "Amber (evidence not yet produced)"
        out.append("| %s | %s | %s | %s |" % (
            item, ", ".join(item_tasks), rating,
            ", ".join("`%s`" % p for p in present) or "none"))
    return out + [""]


def buddies(tasks: Dict[str, Task]) -> Tuple[List[str], int]:
    """Per-buddy owned tasks and artefacts."""
    out = ["## Per buddy: owned tasks, artefacts, decisions", ""]
    linked = 0
    for name, code in BUDDIES.items():
        owned = sorted((t for t in tasks.values() if t.owner == name),
                       key=lambda t: t.task_id)
        arts = sorted({p for t in owned for p in status_of(t)[1]})
        linked += 1 if arts else 0
        out += ["### %s (%s)" % (name, code), "",
                "- Tasks: %s" % ", ".join(
                    "%s (%s)" % (t.task_id, WORK.get(t.task_id, "not started"))
                    for t in owned),
                "- Artefacts present: %s" % (
                    ", ".join("`%s`" % a for a in arts) or "none yet"),
                "- Decisions taken: see the PROVISIONAL list for %s in "
                "`docs/status/continuation_plan.md` section 4; each becomes "
                "a decision only when %s confirms it." % (name, name), ""]
    return out, linked


def trace(tasks: Dict[str, Task]) -> List[str]:
    """FR/NFR to task to acceptance test."""
    out = ["## Requirement to task to test", "",
           "| Requirement | Task | Acceptance test (plan row) | Result |",
           "|---|---|---|---|"]
    for task in sorted(tasks.values(), key=lambda t: t.task_id):
        for req in task.reqs:
            out.append("| %s | %s | %s | %s |" % (
                req, task.task_id, task.acceptance.replace("|", "/")[:200],
                status_of(task)[0]))
    return out + [""]


def ai_notes() -> List[str]:
    """AI-use notes table for the owners (T-B1-07 row)."""
    return ["## AI-use notes", "",
            "The plan row asks for the prompt, the output and the accept or "
            "reject reason for any AI output used while implementing. Code "
            "in the hand-off patches was drafted in AI-assisted sessions "
            "from `docs/prompts/SE15_Complete_All_Prompt.md`; each owner "
            "records here what they kept, changed or rejected and why.", "",
            "| Module or document | Session | Prompt | Owner's accept or "
            "reject reason |", "|---|---|---|---|",
            "| patches 01 to 08 | Ben's session, 29 Oct 2026 | "
            "`docs/prompts/SE15_Complete_All_Prompt.md` | |",
            "| patches 09 to 14 | Jia Jun's session, 29 to 30 Oct 2026 | "
            "same, resumed from `docs/status/continuation_plan.md` | |", ""]


def main(argv: Optional[List[str]] = None) -> int:
    """Write the index and print the pass check."""
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--inputs", required=True,
                        help="hand-off inputs/ folder")
    args = parser.parse_args(argv)
    text, counts = build(args.inputs)
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w", encoding="utf-8") as handle:
        handle.write(text)
    print("wrote %s: %s" % (os.path.relpath(OUT, ROOT), counts))
    return 0


if __name__ == "__main__":
    sys.exit(main())
