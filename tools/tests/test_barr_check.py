"""Tests for tools/barr_check.py: a clean fixture gives 0 findings, and each
fixture below adds exactly one deliberate violation, which must be reported
under exactly its Barr-C:2018 rule number. Run: pytest tools/tests"""

import os
import sys

import pytest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
import barr_check  # noqa: E402

CLEAN_C = """\
/**
 * @file demo.c
 * @brief Clean fixture for the barr_check tests.
 */
#include "demo.h"

#include <stdbool.h>
#include <stdint.h>

#define DEMO_LIMIT 10u

static uint32_t g_count = 0u;
@FILE@
/**
 * @brief Add one to the counter, saturating at DEMO_LIMIT.
 * @return The new count.
 */
uint32_t demo_step (void)
{
    if (DEMO_LIMIT > g_count)
    {
        g_count++;
    }
@BODY@
    return g_count;
}

/*** end of file ***/
"""

CLEAN_H = """\
/**
 * @file demo.h
 * @brief Clean header fixture.
 */
#ifndef DEMO_H
#define DEMO_H

#include <stdint.h>

uint32_t demo_step (void);

#endif /* DEMO_H */

/*** end of file ***/
"""


def run(tmp_path, text, name="demo.c", deviations=None):
    path = tmp_path / name
    path.write_text(text, encoding="utf-8")
    findings = barr_check.check_file(str(path), deviations or {})
    return [f.rule for f in findings]


def c_file(body="", file_level=""):
    return CLEAN_C.replace("@BODY@", body).replace("@FILE@", file_level)


def test_clean_c_has_no_findings(tmp_path):
    assert [] == run(tmp_path, c_file())


def test_clean_h_has_no_findings(tmp_path):
    assert [] == run(tmp_path, CLEAN_H, "demo.h")


BODY_CASES = [
    ("8.6.a", "    if (g_count == 3u)\n    {\n        g_count = 0u;\n    }\n"),
    ("8.6.a", "    if (g_count != DEMO_LIMIT)\n    {\n"
              "        g_count = 0u;\n    }\n"),
    ("1.7.c", "    goto finish;\nfinish:\n    g_count = 0u;\n"),
    ("1.7.d", "    for (uint32_t idx = 0u; DEMO_LIMIT > idx; idx++)\n    {\n"
              "        continue;\n    }\n"),
    ("8.4.c", "    while (1)\n    {\n        g_count++;\n    }\n"),
    ("8.4.c", "    while (true)\n    {\n        g_count++;\n    }\n"),
    ("8.1.a", "    uint32_t first = 0u, second = 0u;\n"),
    ("8.2.d", "    if (DEMO_LIMIT > g_count)\n    {\n        g_count++;\n"
              "    }\n    else if (DEMO_LIMIT < g_count)\n    {\n"
              "        g_count--;\n    }\n"),
    ("5.2.a", "    int value = 0;\n"),
    ("5.2.a", "    unsigned value = 0u;\n"),
    ("5.2.a", "    short value = 0;\n"),
    ("5.2.a", "    long value = 0;\n"),
    ("1.6.a", "    g_count = (uint32_t)g_count;\n"),
    ("8.5.b", "    abort();\n"),
    ("8.5.b", "    exit(1);\n"),
    ("7.1.e", "    uint32_t ab = 0u;\n"),
    ("1.2.a", "    g_count = 0u; " + "/" * 2 + " " + "x" * 64 + "\n"),
    ("3.5.a", "\tg_count = 0u;\n"),
    ("2.1.b", "    g_count = 0u; /" + "/ see http:/" + "/example\n"),
    ("3.3.a", "    g_count = 1u; g_count = 2u;\n"),
    ("7.1.k", "    uint32_t * value = &g_count;\n"),
    ("7.1.m", "    bool done_yet = false;\n"),
    ("1.3.a", "    if (DEMO_LIMIT > g_count) g_count++;\n"),
]


@pytest.mark.parametrize("rule,body", BODY_CASES)
def test_body_violation_is_caught(tmp_path, rule, body):
    assert [rule] == run(tmp_path, c_file(body=body))


FILE_CASES = [
    ("6.1.i", "\nuint32_t step_other (void)\n{\n    return g_count;\n}\n"),
    ("7.1.j", "static uint32_t count_two = 0u;\n"),
    ("6.1.f", "#define demo_bad 1u\n"),
    ("5.1.a", "typedef uint32_t demo_count;\n"),
    ("5.1.b", "\nstruct demo_pair\n{\n    uint32_t first;\n};\n"),
]


@pytest.mark.parametrize("rule,text", FILE_CASES)
def test_file_level_violation_is_caught(tmp_path, rule, text):
    assert [rule] == run(tmp_path, c_file(file_level=text))


def test_pragma_once_is_caught(tmp_path):
    text = CLEAN_H.replace("#ifndef DEMO_H\n#define DEMO_H\n", "#pragma once\n")
    text = text.replace("#endif /* DEMO_H */\n", "")
    assert ["4.2.b", "4.2.b"] == run(tmp_path, text, "demo.h")


def test_missing_end_of_file_comment_is_caught(tmp_path):
    text = c_file().replace("/*** end of file ***/\n", "")
    assert ["3.3.c"] == run(tmp_path, text)


def test_declared_deviation_is_accepted(tmp_path):
    body = "    int value = 0; /* DEVIATION D-9: vendor return type. */\n"
    assert [] == run(tmp_path, c_file(body=body),
                     deviations={"D-9": {"5.2.a"}})


def test_deviation_for_another_rule_is_not_accepted(tmp_path):
    body = "    int value = 0; /* DEVIATION D-9: vendor return type. */\n"
    assert ["5.2.a"] == run(tmp_path, c_file(body=body),
                            deviations={"D-9": {"1.6.a"}})


def test_unknown_deviation_is_a_finding(tmp_path):
    body = "    int value = 0; /* DEVIATION D-8: not in the table. */\n"
    rules = run(tmp_path, c_file(body=body), deviations={"D-9": {"5.2.a"}})
    assert sorted(rules) == ["5.2.a", "deviation"]


def test_deviation_table_is_parsed(tmp_path):
    table = tmp_path / "standard.md"
    table.write_text("| ID | Rule |\n|---|---|\n| D-1 | 1.1.a | x |\n"
                     "| D-2 | 5.2.a, 6.3.a | y |\n", encoding="utf-8")
    assert {"D-1": {"1.1.a"}, "D-2": {"5.2.a", "6.3.a"}} == \
        barr_check.load_deviations(str(table))
