"""pytest: the requirement to test map in tools/traceability.py.

These checks need no build: every requirement in docs/requirements.md has a
row, every selector names a test that exists, and TRACEABILITY.md matches
the map. Running the selected tests is tools/traceability.py's job.
"""

from __future__ import annotations

import glob
import os
import re
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
import traceability as tr  # noqa: E402

ROOT = tr.ROOT


def read(path: str) -> str:
    """A repository file as text."""
    with open(os.path.join(ROOT, path), encoding="utf-8") as handle:
        return handle.read()


def test_every_requirement_has_one_row() -> None:
    listed = re.findall(r"^\| (N?FR\d+) \|", read("docs/requirements.md"),
                        re.MULTILINE)
    mapped = [row[0] for row in tr.REQUIREMENTS]
    assert listed == mapped


def test_every_row_is_covered_or_says_why_not() -> None:
    for req_id, _, cases, wall, note in tr.REQUIREMENTS:
        assert cases or wall or note, req_id
        assert all(test in tr.WALL for test in wall), req_id


def test_c_selectors_name_a_case_in_their_suite() -> None:
    for req_id, _, cases, _, _ in tr.REQUIREMENTS:
        for suite, selector in cases:
            if suite == "py":
                continue
            sources = "".join(
                read(os.path.relpath(path, ROOT)) for path in glob.glob(
                    os.path.join(ROOT, tr.SUITES[suite], "*.c")))
            assert '"' + selector in sources, (req_id, suite, selector)


def test_py_selectors_name_a_test_that_exists() -> None:
    for req_id, _, cases, _, _ in tr.REQUIREMENTS:
        for suite, selector in cases:
            if suite != "py":
                continue
            path, _, name = selector.partition("::")
            text = read(path)
            assert not name or re.search(
                r"^def %s\(" % re.escape(name), text, re.MULTILINE), (
                req_id, selector)


def test_traceability_md_is_current() -> None:
    assert read("docs/testing/TRACEABILITY.md") == tr.markdown()


def test_c_selector_takes_a_whole_token() -> None:
    assert tr.selects("C18 re-plan: hit", "C18")
    assert not tr.selects("C18b re-plan: tap", "C18")
    assert tr.selects("Test 1: slew", "Test 1")
    assert not tr.selects("Test 12: other", "Test 1")
