#!/usr/bin/env python3
"""Barr-C:2018 checker for the SE15 C files that opted in.

Usage:
    python3 tools/barr_check.py              check every file in tools/barr_scope.txt
    python3 tools/barr_check.py FILE...      check the given files only

Exit status 0 when there are no findings, 1 when there is at least one,
2 on a usage or configuration error.

The checker covers the rules that a pattern match can decide. Layout rules
(braces, indentation, spaces, alignment) are left to clang-format with the
repository .clang-format; the rest are code-review rules listed in
docs/coding_standard.md. Rule numbers are Barr-C:2018 sections.

A finding is accepted only under a declared deviation: the line, or the
comment-only line directly above it, carries "DEVIATION D-<n>" in a comment,
and D-<n> is a row of the deviation table in docs/coding_standard.md whose
rule column names the finding's rule. Unknown IDs are findings themselves.
"""

import os
import re
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SCOPE_FILE = os.path.join(REPO, "tools", "barr_scope.txt")
STANDARD_FILE = os.path.join(REPO, "docs", "coding_standard.md")

MAX_COLUMNS = 80
MIN_NAME_LEN = 3
MAX_NAME_LEN = 31

# Functions whose names the C standard or the vendor SDK fixes.
FIXED_FUNCTION_NAMES = {"main", "app_main"}

KEYWORDS = {
    "if", "else", "for", "while", "do", "switch", "case", "default", "break",
    "continue", "return", "goto", "sizeof", "typedef", "struct", "union",
    "enum", "static", "const", "volatile", "extern", "inline", "register",
    "auto", "_Atomic", "_Static_assert", "_Alignof", "_Bool", "restrict",
}

QUALIFIERS = {"static", "const", "volatile", "extern", "inline", "register"}

BASE_TYPES = {
    "void", "bool", "char", "float", "double", "int", "short", "long",
    "unsigned", "signed", "_Bool",
}

CONST_RE = re.compile(
    r"^(?:0[xX][0-9a-fA-F]+[uUlL]*|\d+[uUlL]*|'c+'|NULL|true|false"
    r"|[A-Z][A-Z0-9_]*|pd[A-Z][A-Z_]*)$")

TOKEN_RE = re.compile(
    r"[A-Za-z_]\w*|0[xX][0-9a-fA-F]+[uUlL]*|\d+[uUlL]*|'c+'|\"[^\"]*\""
    r"|==|!=|<=|>=|&&|\|\||->|\+\+|--|<<|>>|[^\s]")

TYPE_NAME = (r"(?:u?int(?:8|16|32|64)_t|bool|char|void|float|double|size_t"
             r"|ssize_t|u?intptr_t|int|unsigned(?:\s+int)?|long|short"
             r"|struct\s+\w+|[a-z_]\w*_t|[A-Z]\w*_t)")

CAST_RE = re.compile(
    r"(?<![\w\]])\(\s*(?:(?:const|volatile)\s+)*" + TYPE_NAME +
    r"(?:\s*\*)*\s*\)\s*(?=[\w(\-~!&*'])")


class Finding(object):
    """One rule violation at one line of one file."""

    def __init__(self, path, line, rule, message):
        self.path = path
        self.line = line
        self.rule = rule
        self.message = message

    def __str__(self):
        return "%s:%d: [%s] %s" % (self.path, self.line, self.rule,
                                   self.message)


class Source(object):
    """A C file split into code text (comments and literals blanked) and
    comment text, line by line."""

    def __init__(self, path, text):
        self.path = path
        self.raw = text.split("\n")
        self.code, self.comments = split_code_and_comments(text)
        self.preproc = mark_preprocessor(self.code)
        self.comment_only = [
            bool(self.comments[i].strip()) and not self.code[i].strip()
            for i in range(len(self.code))]


def split_code_and_comments(text):
    """Blank comments and literal contents; return per-line code and comment
    text. Line structure and columns are kept."""
    code = []
    comment = []
    line_code = []
    line_comment = []
    state = "code"
    i = 0
    length = len(text)
    while i < length:
        ch = text[i]
        nxt = text[i + 1] if (i + 1) < length else ""
        if "\n" == ch:
            if "line_comment" == state:
                state = "code"
            code.append("".join(line_code))
            comment.append("".join(line_comment))
            line_code = []
            line_comment = []
        elif "code" == state:
            if ("/" == ch) and ("/" == nxt):
                state = "line_comment"
                line_code.append("  ")
                i += 1
            elif ("/" == ch) and ("*" == nxt):
                state = "block_comment"
                line_code.append("  ")
                i += 1
            elif "\"" == ch:
                state = "string"
                line_code.append(ch)
            elif "'" == ch:
                state = "char"
                line_code.append(ch)
            else:
                line_code.append(ch)
        elif "line_comment" == state:
            line_comment.append(ch)
            line_code.append(" ")
        elif "block_comment" == state:
            if ("*" == ch) and ("/" == nxt):
                state = "code"
                line_code.append("  ")
                i += 1
            else:
                line_comment.append(ch)
                line_code.append(" ")
        else:
            quote = "\"" if ("string" == state) else "'"
            filler = " " if ("string" == state) else "c"
            if ("\\" == ch) and ("\n" != nxt) and ("" != nxt):
                line_code.append(filler + filler)
                i += 1
            elif quote == ch:
                state = "code"
                line_code.append(ch)
            else:
                line_code.append(filler)
        i += 1
    code.append("".join(line_code))
    comment.append("".join(line_comment))
    return code, comment


def mark_preprocessor(code):
    """True for every line that belongs to a preprocessor directive,
    including backslash continuation lines."""
    marks = []
    continued = False
    for line in code:
        is_pp = continued or line.lstrip().startswith("#")
        marks.append(is_pp)
        continued = is_pp and line.rstrip().endswith("\\")
    return marks


def load_deviations(path):
    """Map deviation ID to the set of rule numbers it covers, read from the
    deviation table in docs/coding_standard.md."""
    deviations = {}
    if os.path.exists(path):
        row_re = re.compile(r"^\|\s*(D-\d+)\s*\|\s*([^|]*)\|")
        with open(path, encoding="utf-8") as handle:
            for line in handle:
                match = row_re.match(line)
                if match:
                    rules = set(re.findall(r"\d+\.\d+\.[a-z]", match.group(2)))
                    deviations[match.group(1)] = rules
    return deviations


def module_name(path):
    return os.path.splitext(os.path.basename(path))[0]


def tokens_with_lines(src):
    """Tokens of the code text outside preprocessor lines, each with its
    1-based line number."""
    result = []
    for index, line in enumerate(src.code):
        if src.preproc[index]:
            continue
        for match in TOKEN_RE.finditer(line):
            result.append((match.group(0), index + 1))
    return result


def matching(tokens, start, open_tok, close_tok):
    """Index just after the token that closes tokens[start]."""
    depth = 0
    index = start
    while index < len(tokens):
        tok = tokens[index][0]
        if open_tok == tok:
            depth += 1
        elif close_tok == tok:
            depth -= 1
            if 0 == depth:
                return index + 1
        index += 1
    return len(tokens)


def skip_statement(tokens, start):
    """Index just after one statement or braced block starting at start."""
    if start >= len(tokens):
        return start
    if "{" == tokens[start][0]:
        return matching(tokens, start, "{", "}")
    if "if" == tokens[start][0]:
        index = matching(tokens, start + 1, "(", ")")
        index = skip_statement(tokens, index)
        if (index < len(tokens)) and ("else" == tokens[index][0]):
            index = skip_statement(tokens, index + 1)
        return index
    if tokens[start][0] in ("for", "while", "switch"):
        index = matching(tokens, start + 1, "(", ")")
        return skip_statement(tokens, index)
    depth = 0
    index = start
    while index < len(tokens):
        tok = tokens[index][0]
        if tok in ("(", "[", "{"):
            depth += 1
        elif tok in (")", "]", "}"):
            depth -= 1
        elif (";" == tok) and (0 == depth):
            return index + 1
        index += 1
    return index


# ---------------------------------------------------------------------------
# Line rules
# ---------------------------------------------------------------------------

def check_lines(src, out):
    for index, raw in enumerate(src.raw):
        number = index + 1
        if len(raw.rstrip("\r")) > MAX_COLUMNS:
            out.append(Finding(src.path, number, "1.2.a",
                               "line is %d characters, limit %d"
                               % (len(raw), MAX_COLUMNS)))
        if "\t" in raw:
            out.append(Finding(src.path, number, "3.5.a", "tab character"))
        code = src.code[index]
        if re.match(r"\s*#\s*pragma\s+once\b", code):
            out.append(Finding(src.path, number, "4.2.b",
                               "#pragma once instead of an #ifndef guard"))
        text = src.comments[index]
        if ("//" in text) or ("/*" in text) or ("\\" in text):
            out.append(Finding(src.path, number, "2.1.b",
                               "comment contains //, /* or a backslash"))
        if re.search(r"\b(goto)\b", code):
            out.append(Finding(src.path, number, "1.7.c", "goto"))
        if re.search(r"\b(continue)\b", code):
            out.append(Finding(src.path, number, "1.7.d", "continue"))
        if re.search(r"\bwhile\s*\(\s*(1|true)\s*\)", code):
            out.append(Finding(src.path, number, "8.4.c",
                               "infinite loop not written as for (;;)"))
        if re.search(r"(?<![\w.>])(abort|exit|setjmp|longjmp)\s*\(", code):
            out.append(Finding(src.path, number, "8.5.b",
                               "abort, exit, setjmp or longjmp"))
        for match in re.finditer(r"\b(int|short|long|unsigned|signed)\b",
                                 code):
            out.append(Finding(src.path, number, "5.2.a",
                               "plain '%s'; use a fixed-width type"
                               % match.group(1)))
        check_equality(src, index, out)
        check_casts(src, index, out)
        check_statements_per_line(src, index, out)
        check_macro_name(src, index, out)
    check_guard(src, out)
    check_end_of_file(src, out)


def check_equality(src, index, out):
    tokens = [m.group(0) for m in TOKEN_RE.finditer(src.code[index])]
    for pos, tok in enumerate(tokens):
        if tok not in ("==", "!="):
            continue
        left = tokens[pos - 1] if pos > 0 else ""
        right = tokens[pos + 1] if (pos + 1) < len(tokens) else ""
        if CONST_RE.match(right) and not CONST_RE.match(left):
            rule = "8.6.a"
            note = ""
            if "!=" == tok:
                note = " (project extension of 8.6.a to !=)"
            out.append(Finding(src.path, index + 1, rule,
                               "constant on the right of %s%s" % (tok, note)))


def statement_start(src, index):
    """First line of the statement that line index belongs to: walk back
    while the previous code line does not end a statement or block."""
    start = index
    while start > 0:
        prev = src.code[start - 1].strip()
        if (not prev) or src.preproc[start - 1] or prev.endswith(
                (";", "{", "}")):
            break
        start -= 1
    return start


def has_comment_near(src, index):
    """A comment on any line of the statement, or a comment-only line
    directly above the statement."""
    start = statement_start(src, index)
    found = any(src.comments[i].strip() for i in range(start, index + 1))
    if (not found) and (start > 0):
        found = src.comment_only[start - 1]
    return found


def check_casts(src, index, out):
    code = src.code[index]
    for match in CAST_RE.finditer(code):
        before = code[:match.start()].rstrip()
        if before.endswith("sizeof") or before.endswith("_Atomic"):
            continue
        if not has_comment_near(src, index):
            out.append(Finding(src.path, index + 1, "1.6.a",
                               "cast %s without a comment"
                               % match.group(0).strip()))


def check_statements_per_line(src, index, out):
    if src.preproc[index]:
        return
    depth = 0
    count = 0
    for ch in src.code[index]:
        if "(" == ch:
            depth += 1
        elif ")" == ch:
            depth -= 1
        elif (";" == ch) and (depth <= 0):
            count += 1
    if count > 1:
        out.append(Finding(src.path, index + 1, "3.3.a",
                           "more than one statement on the line"))


def check_macro_name(src, index, out):
    match = re.match(r"\s*#\s*define\s+(\w+)", src.code[index])
    if match and re.search(r"[a-z]", match.group(1)):
        out.append(Finding(src.path, index + 1, "6.1.f",
                           "macro name '%s' contains lowercase"
                           % match.group(1)))


def check_guard(src, out):
    if not src.path.endswith(".h"):
        return
    directives = [line.strip() for i, line in enumerate(src.code)
                  if src.preproc[i] and line.strip()]
    ok = ((len(directives) >= 2) and directives[0].startswith("#ifndef")
          and directives[1].startswith("#define")
          and (directives[0].split()[-1] == directives[1].split()[1]))
    if not ok:
        out.append(Finding(src.path, 1, "4.2.b",
                           "header does not start with an #ifndef guard"))


def check_end_of_file(src, out):
    last = len(src.raw) - 1
    while (last >= 0) and (not src.raw[last].strip()):
        last -= 1
    ok = (last >= 0) and src.comment_only[last] and (
        "end of file" in src.comments[last].lower())
    if not ok:
        out.append(Finding(src.path, max(last + 1, 1), "3.3.c",
                           "file does not end with an end-of-file comment"))


# ---------------------------------------------------------------------------
# Token rules: if/else chains, declarations, functions
# ---------------------------------------------------------------------------

def check_if_chains(src, tokens, out):
    for index, (tok, line) in enumerate(tokens):
        if ("if" != tok) or ((index > 0) and ("else" == tokens[index - 1][0])):
            continue
        has_else_if = False
        has_else = False
        pos = index
        while True:
            body = matching(tokens, pos + 1, "(", ")")
            if (body < len(tokens)) and ("{" != tokens[body][0]):
                out.append(Finding(src.path, tokens[body][1], "1.3.a",
                                   "if or else body without braces"))
            after = skip_statement(tokens, body)
            if (after < len(tokens)) and ("else" == tokens[after][0]):
                if ((after + 1) < len(tokens)) and (
                        "if" == tokens[after + 1][0]):
                    has_else_if = True
                    pos = after + 1
                else:
                    has_else = True
                    break
            else:
                break
        if has_else_if and not has_else:
            out.append(Finding(src.path, line, "8.2.d",
                               "if ... else if chain without a final else"))


def split_top_level(tokens, separator):
    parts = [[]]
    depth = 0
    for item in tokens:
        tok = item[0]
        if tok in ("(", "[", "{"):
            depth += 1
        elif tok in (")", "]", "}"):
            depth -= 1
        if (separator == tok) and (0 == depth):
            parts.append([])
        else:
            parts[-1].append(item)
    return parts


def strip_qualifiers(tokens):
    """Drop leading qualifiers and _Atomic(...); return (rest, is_static)."""
    is_static = False
    index = 0
    while index < len(tokens):
        tok = tokens[index][0]
        if "static" == tok:
            is_static = True
            index += 1
        elif tok in QUALIFIERS:
            index += 1
        elif ("_Atomic" == tok) and ((index + 1) < len(tokens)) and (
                "(" == tokens[index + 1][0]):
            break
        elif "_Atomic" == tok:
            index += 1
        else:
            break
    return tokens[index:], is_static


def type_length(tokens):
    """Number of tokens forming the type at the start, or 0 if the tokens do
    not start with a type."""
    if not tokens:
        return 0
    first = tokens[0][0]
    if first in ("struct", "union", "enum"):
        if (len(tokens) > 1) and re.match(r"[A-Za-z_]\w*$", tokens[1][0]):
            return 2
        return 0
    if ("_Atomic" == first) and (len(tokens) > 1) and ("(" == tokens[1][0]):
        return matching(tokens, 1, "(", ")")
    count = 0
    while (count < len(tokens)) and (tokens[count][0] in BASE_TYPES):
        count += 1
    if count > 0:
        return count
    if re.match(r"[A-Za-z_]\w*_t$", first):
        return 1
    return 0


def check_declarator(src, decl, line, out, is_param, is_file_scope):
    """decl: tokens of one declarator after the type."""
    stars = 0
    index = 0
    while (index < len(decl)) and (decl[index][0] in ("*", "const",
                                                      "volatile")):
        if "*" == decl[index][0]:
            stars += 1
        index += 1
    if (index >= len(decl)) or not re.match(r"[A-Za-z_]\w*$",
                                            decl[index][0]):
        return
    name = decl[index][0]
    line = decl[index][1]
    if name in KEYWORDS:
        return
    if len(name) < MIN_NAME_LEN:
        out.append(Finding(src.path, line, "7.1.e",
                           "name '%s' is shorter than %d characters"
                           % (name, MIN_NAME_LEN)))
    if len(name) > MAX_NAME_LEN:
        out.append(Finding(src.path, line, "7.1.d",
                           "name '%s' is longer than %d characters"
                           % (name, MAX_NAME_LEN)))
    if re.search(r"[A-Z]", name):
        out.append(Finding(src.path, line, "7.1.f",
                           "name '%s' contains uppercase" % name))
    bare = name[2:] if name.startswith("g_") else name
    if (2 <= stars) and not bare.startswith("pp_"):
        out.append(Finding(src.path, line, "7.1.l",
                           "pointer-to-pointer '%s' does not start with pp_"
                           % name))
    elif (1 == stars) and not bare.startswith("p_"):
        out.append(Finding(src.path, line, "7.1.k",
                           "pointer '%s' does not start with p_" % name))
    if is_file_scope and (not is_param) and not name.startswith("g_"):
        out.append(Finding(src.path, line, "7.1.j",
                           "file-scope variable '%s' does not start with g_"
                           % name))
    return name


def check_declaration(src, stmt, out, is_file_scope):
    """stmt: tokens of one statement (no trailing ';')."""
    rest, is_static = strip_qualifiers(stmt)
    if rest and ("typedef" == rest[0][0]):
        return
    tlen = type_length(rest)
    if 0 == tlen:
        return
    type_tokens = rest[:tlen]
    body = rest[tlen:]
    if not body:
        return
    is_bool = any("bool" == t[0] for t in type_tokens)
    parts = split_top_level(body, ",")
    first_paren = next((i for i, t in enumerate(body) if "(" == t[0]), None)
    first_comma = next((i for i, t in enumerate(body) if "," == t[0]), None)
    is_function = (first_paren is not None) and (
        (first_comma is None) or (first_paren < first_comma)) and (
        not any("=" == t[0] for t in body[:first_paren]))
    if is_function:
        check_function(src, body, out, is_static, is_file_scope)
        return
    if len(parts) > 1:
        out.append(Finding(src.path, body[0][1], "8.1.a",
                           "comma in a variable declaration"))
    for part in parts:
        declarator = []
        for item in part:
            if item[0] in ("=", "["):
                break
            declarator.append(item)
        name = check_declarator(src, declarator, part[0][1] if part else 0,
                                out, False, is_file_scope)
        bare = (name[2:] if (name and name.startswith("g_")) else name)
        if name and is_bool and not bare.startswith("b_"):
            out.append(Finding(src.path, part[0][1], "7.1.m",
                               "bool variable '%s' does not start with b_"
                               % name))


def check_function(src, body, out, is_static, is_file_scope):
    stars = 0
    index = 0
    while (index < len(body)) and ("*" == body[index][0]):
        stars += 1
        index += 1
    if (index >= len(body)) or ("(" == body[index][0]):
        return
    name, line = body[index]
    if ((index + 1) >= len(body)) or ("(" != body[index + 1][0]):
        return
    if is_file_scope:
        module = module_name(src.path)
        if re.search(r"[A-Z]", name):
            out.append(Finding(src.path, line, "6.1.e",
                               "function '%s' contains uppercase" % name))
        if len(name) > MAX_NAME_LEN:
            out.append(Finding(src.path, line, "6.1.d",
                               "function '%s' is longer than %d characters"
                               % (name, MAX_NAME_LEN)))
        if ((not is_static) and (name not in FIXED_FUNCTION_NAMES)
                and not name.startswith(module + "_")):
            out.append(Finding(src.path, line, "6.1.i",
                               "public function '%s' lacks the prefix '%s_'"
                               % (name, module)))
    close = matching(body, index + 1, "(", ")")
    params = body[index + 2:close - 1]
    for param in split_top_level(params, ","):
        rest, _ = strip_qualifiers(param)
        tlen = type_length(rest)
        if (0 == tlen) or (tlen >= len(rest)):
            continue
        if "(" in [t[0] for t in rest[tlen:]]:
            continue
        name = check_declarator(src, rest[tlen:], rest[0][1], out, True,
                                False)
        is_bool = any("bool" == t[0] for t in rest[:tlen])
        if name and is_bool and not name.startswith("b_"):
            out.append(Finding(src.path, rest[0][1], "7.1.m",
                               "bool parameter '%s' does not start with b_"
                               % name))


def check_tokens(src, out):
    tokens = tokens_with_lines(src)
    check_if_chains(src, tokens, out)
    # Walk statements with a brace stack. Kinds: "aggregate" (struct, union,
    # enum or initialiser body), "func", "block".
    stack = []
    stmt = []
    pending_typedef = None
    index = 0
    while index < len(tokens):
        tok = tokens[index][0]
        in_aggregate = bool(stack) and ("aggregate" == stack[-1])
        if "{" == tok:
            words = [t[0] for t in stmt]
            is_init = bool(words) and (words[-1] in ("=", ",", "{"))
            tags = ("struct", "union", "enum")
            is_type = bool(words) and ((words[-1] in tags) or (
                (len(words) > 1) and (words[-2] in tags)))
            if is_type and (not is_init) and (not in_aggregate):
                if "typedef" in words:
                    pending_typedef = len(stack)
                else:
                    out.append(Finding(src.path, stmt[0][1], "5.1.b",
                                       "struct, union or enum not named "
                                       "via a typedef"))
            if is_type or is_init or in_aggregate:
                stack.append("aggregate")
            elif (not stack) and words and (")" == words[-1]):
                check_declaration(src, stmt, out, True)
                stack.append("func")
            else:
                if stmt:
                    check_control_header(src, stmt, out)
                stack.append("block")
            stmt = []
        elif "}" == tok:
            if stack:
                stack.pop()
            stmt = []
        elif ";" == tok:
            if (pending_typedef is not None) and (
                    len(stack) == pending_typedef):
                check_typedef_name(src, stmt, out)
                pending_typedef = None
            elif stmt and ("typedef" == stmt[0][0]):
                check_typedef_name(src, stmt, out)
            elif not in_aggregate:
                check_statement(src, stmt, out, not stack)
            stmt = []
        else:
            stmt.append(tokens[index])
        index += 1


def check_typedef_name(src, stmt, out):
    """The name a typedef introduces: lowercase, ending in _t (5.1.a)."""
    words = [t[0] for t in stmt]
    name = None
    line = stmt[0][1] if stmt else 0
    for pos in range(len(words) - 1):
        if ("(" == words[pos]) and ("*" == words[pos + 1]) and (
                (pos + 2) < len(words)):
            name = words[pos + 2]
            line = stmt[pos + 2][1]
            break
    if name is None:
        idents = [t for t in stmt if re.match(r"[A-Za-z_]\w*$", t[0])]
        if idents:
            name, line = idents[-1]
    if name and not re.match(r"[a-z][a-z0-9_]*_t$", name):
        out.append(Finding(src.path, line, "5.1.a",
                           "type name '%s' is not lowercase ending in _t"
                           % name))


def check_control_header(src, stmt, out):
    """A for statement whose init clause declares a variable."""
    words = [t[0] for t in stmt]
    if words and ("for" == words[0]) and (len(stmt) > 2):
        close = matching(stmt, 1, "(", ")")
        inner = split_top_level(stmt[2:close - 1], ";")
        if inner and inner[0]:
            check_declaration(src, inner[0], out, False)


def check_statement(src, stmt, out, is_file_scope):
    words = [t[0] for t in stmt]
    if not words:
        return
    if "for" == words[0]:
        check_control_header(src, stmt, out)
        return
    if words[0] in ("return", "if", "while", "switch", "case", "do", "else",
                    "break", "goto", "continue", "default"):
        return
    check_declaration(src, stmt, out, is_file_scope)


# ---------------------------------------------------------------------------
# Driver
# ---------------------------------------------------------------------------

def apply_deviations(src, findings, deviations):
    kept = []
    for finding in findings:
        index = finding.line - 1
        texts = [src.comments[index]] if (index < len(src.comments)) else []
        if (index > 0) and src.comment_only[index - 1]:
            texts.append(src.comments[index - 1])
        ids = []
        for text in texts:
            ids.extend(re.findall(r"DEVIATION (D-\d+)", text))
        accepted = False
        for dev_id in ids:
            if dev_id not in deviations:
                kept.append(Finding(src.path, finding.line, "deviation",
                                    "%s is not in the deviation table"
                                    % dev_id))
            elif finding.rule in deviations[dev_id]:
                accepted = True
        if not accepted:
            kept.append(finding)
    return kept


def check_file(path, deviations):
    with open(path, encoding="utf-8") as handle:
        text = handle.read()
    rel = os.path.relpath(path, REPO) if os.path.isabs(path) else path
    src = Source(rel, text)
    findings = []
    check_lines(src, findings)
    check_tokens(src, findings)
    findings = apply_deviations(src, findings, deviations)
    findings.sort(key=lambda f: (f.line, f.rule))
    return findings


def read_scope(path):
    files = []
    with open(path, encoding="utf-8") as handle:
        for line in handle:
            entry = line.split("#", 1)[0].strip()
            if entry:
                files.append(os.path.join(REPO, entry))
    return files


def main(argv):
    deviations = load_deviations(STANDARD_FILE)
    files = argv[1:] if (len(argv) > 1) else read_scope(SCOPE_FILE)
    status = 0
    for path in files:
        if not os.path.exists(path):
            print("barr_check: no such file: %s" % path, file=sys.stderr)
            return 2
        for finding in check_file(path, deviations):
            print(finding)
            status = 1
    if 0 == status:
        print("barr_check: %d file(s), 0 findings" % len(files))
    return status


if __name__ == "__main__":
    sys.exit(main(sys.argv))
