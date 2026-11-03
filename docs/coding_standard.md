# SE15 coding standard: Barr-C:2018

The lecturer requires the Barr Group *Embedded C Coding Standard*, edition BARR-C:2018 (Michael Barr, ISBN 978-1-72112-798-6, free PDF at barrgroup.com/coding-standard). Barr-C is mandatory for every C file in this repository that is listed in `tools/barr_scope.txt`. Each buddy opts a file in by adding it there once it is clean.

Every rule number below was checked against the 2018 PDF. The PDF licence allows internal use only and this repository is public, so the table gives each rule in our own short words, not the book's text. Read the book for the full rule, its reasoning and its examples.

## 1. How the rules are checked

| Tag | Tool | Command (from the repository root) |
|---|---|---|
| CF | clang-format 18.1.8 with `.clang-format` | `clang-format --dry-run --Werror <files>` (install with `pip install clang-format==18.1.8`; other versions format differently) |
| BC | `tools/barr_check.py` | `python3 tools/barr_check.py` (all files in `tools/barr_scope.txt`) or `python3 tools/barr_check.py <files>` |
| CC | compiler flags | host tests: `-std=c11 -Wall -Wextra -Werror` (`panel_sync/test/Makefile`); ESP-IDF v5.3 builds C as `gnu17` (`tools/cmake/build.cmake`) |
| RV | code review | the pull-request reviewer checks the rule by reading |

CI job `barr` in `.github/workflows/build.yml` runs CF, BC, the `barr_check` and `skew_analyse` pytest suites and the host tests on every push. A finding fails the job.

## 2. Rules

| Rule | In our words | Check |
|---|---|---|
| 1.1.a | Write C99. | CC; deviation D-1 |
| 1.1.c | Keep compiler extensions, `#pragma` and inline assembly to the minimum, in driver modules only. | RV |
| 1.1.d | Never `#define` a keyword or language feature away. | RV |
| 1.2.a | Lines at most 80 characters. | CF, BC |
| 1.3.a | Braces around every `if`, `else`, `switch`, `while`, `do` and `for` body, even a single statement. | CF (`InsertBraces`), BC for `if`/`else` |
| 1.3.b | Each brace on its own line; the closing brace lines up with the opening one. | CF |
| 1.4.a, 1.4.b | Use parentheses rather than precedence; parenthesise every operand of `&&` and `\|\|` that is not a single identifier or constant (a function call is neither, so it is parenthesised too). | BC (1.4.b, and 1.4.a for `&&` mixed with `\|\|`), RV for the rest of 1.4.a |
| 1.5.a, 1.5.b | Avoid abbreviations; keep a table of project abbreviations. | RV; section 5 |
| 1.6.a | Every cast has a comment saying why the value fits the target type. | BC |
| 1.7.a, 1.7.b | No `auto`, no `register`. | RV |
| 1.7.c, 1.7.d | Avoid `goto` and `continue` (preferred practice). | BC (we treat both as errors) |
| 1.8.a | `static` for every function and variable not needed outside its module. | RV |
| 1.8.b | `const` wherever it applies. | RV |
| 1.8.c | `volatile` wherever it applies (ISR-shared and thread-shared globals, registers, delay counters). | RV; see section 3 |
| 2.1.a | `//` and `/* */` comments are both allowed. | none |
| 2.1.b | A comment never contains `/*`, `//` or a backslash. | BC |
| 2.1.c | No commented-out code; use `#if 0`, and wrap debug-only code in `#ifndef NDEBUG`. | RV |
| 2.2.a to 2.2.g | Comments are full sentences, precede the block they describe, skip the obvious, scale with complexity, cite external sources and spell out assumptions. | RV |
| 2.2.h | Comment every module and function for documentation generation (Doxygen). | RV; templates in `docs/templates/` |
| 2.2.i | Use the capitalised markers (WARNING, NOTE, TODO) for important issues. | RV |
| 3.1.a to 3.1.m | Spacing: one space after `if`/`while`/`for`/`switch`/`return`; spaces round binary and assignment operators; none on the operand side of unary operators; `*` and `&` spaced in declarations; one space between the function name and `(` in a declaration, none in a call. | CF |
| 3.2.a to 3.2.d | Align variable names, member names and assignment operators in a block; `#` at the start of the line. | CF |
| 3.3.a | One statement per line. | BC |
| 3.3.b | Blank line before and after each natural block. | RV |
| 3.3.c | Each file ends with an end-of-file comment. | BC |
| 3.4.a to 3.4.c | Indent by 4; case labels aligned with contents indented once; wrap long lines readably. | CF |
| 3.5.a | No tab characters. | CF, BC |
| 3.6.a, 3.6.b | LF line endings; no other non-printing characters except form feed. | RV (`.gitattributes` not needed: all files are LF) |
| 4.1.a to 4.1.d | Module file names lowercase with underscores, unique in the first 8 characters, not a standard-library header name; a module with `main()` has "main" in its file name. | RV |
| 4.2.a | Exactly one header per source file, same root name. | RV (a host test's `test_main.c` has `test_main.h`, which declares the test runner functions `test_main.c` defines) |
| 4.2.b | Every header has an `#ifndef` guard. | BC |
| 4.2.c, 4.2.d | A header exposes only what other modules need; avoid `extern` variables; no storage in headers; public headers never include private ones. | RV |
| 4.3.a to 4.3.f | One entity per source file; fixed section order; each source includes its own header; no absolute include paths; no unused includes; never include a `.c` file. | RV |
| 4.4.a | Keep project file templates. | `docs/templates/module.h`, `module.c` |
| 5.1.a | Type names lowercase with underscores, ending in `_t`. | BC |
| 5.1.b | Every struct, union and enum is named through a `typedef`. | BC |
| 5.1.c | Public type names start with the module name and an underscore. | RV |
| 5.2.a, 5.2.b | Fixed-width integer types wherever the width matters; never `short` or `long`. | BC (we treat every plain `int`, `short`, `long`, `unsigned` and `signed` as a finding); deviation D-2 |
| 5.2.c | `char` only for strings. | RV |
| 5.3.a to 5.3.c | No signed bit-fields; no bitwise operators on signed data; never mix signed and unsigned in one expression; `u` suffix on unsigned constants. | RV |
| 5.4.a, 5.4.b | Avoid floating point; if needed, follow the listed precautions. | RV |
| 5.5.a, 5.5.b | Stop the compiler inserting padding or reordering bit-fields in anything sent over a network. | RV (wire formats use explicit shifts and masks, PostTri VB-2b) |
| 5.6.a, 5.6.b | Booleans are `bool`; convert to Boolean with a relational operator, not a cast. | RV |
| 6.1.a to 6.1.h | Function names: not a keyword or library name, no leading underscore, at most 31 characters, lowercase with underscores, descriptive. Macro names have no lowercase. | BC (6.1.d, 6.1.e, 6.1.f), RV |
| 6.1.i | Public function names start with the module name and an underscore. | BC |
| 6.2.a to 6.2.f | Functions at most 100 lines; one exit point at the bottom (preferred practice); prototype in the header for each public function; private functions `static`; every parameter named. | BC for 6.2.a (counted strictly, opening brace to closing brace, blank and comment lines included); RV for the rest (we treat 6.2.c as required) |
| 6.3.a, 6.3.b | No function-like macro where a function would do; otherwise parenthesise everything, use each parameter once, never transfer control. | RV; deviation D-2 for vendor macros |
| 6.4.a | Task functions end in `_task` (or `_thread`, `_process`). | RV |
| 6.5.a to 6.5.d | ISRs are marked as such, end in `_isr`, are `static`, and unhandled vectors get a stub. | RV |
| 7.1.a to 7.1.i | Variable names: not a keyword or library name, no leading underscore, 3 to 31 characters, lowercase with underscores, no repeated magic number, descriptive. | BC (7.1.d, 7.1.e, 7.1.f), RV |
| 7.1.j | Global (including file-scope) variable names start with `g_`. | BC |
| 7.1.k, 7.1.l | Pointer names start with `p_`; pointer-to-pointer names with `pp_`. | BC |
| 7.1.m | Boolean variable names start with `b_` and read as a question. | BC |
| 7.2.a to 7.2.d | Initialise before use; declare locals where needed; group globals at the top; uninitialised pointers are `NULL`. | RV |
| 8.1.a | No comma in a variable declaration. | BC |
| 8.2.a to 8.2.c | Shortest branch first (preferred); `if` nesting at most two levels; no assignment inside an `if` test. | RV |
| 8.2.d | An `if` with `else if` ends with an `else`. | BC |
| 8.3.a to 8.3.c | `break` aligned with its `case`; every `switch` has a `default`; fall-through is commented. | CF, RV |
| 8.4.a, 8.4.b | No magic numbers in loop bounds; no assignment in a loop's controlling expression except the counter. | RV |
| 8.4.c | Infinite loops are `for (;;)`. | BC |
| 8.4.d | An empty loop body is braces around a comment. | RV |
| 8.5.a, 8.5.b | `goto` only as 1.7.c allows; never `abort()`, `exit()`, `setjmp()` or `longjmp()`. | BC |
| 8.6.a | Constant on the left of `==`. | BC; we also apply it to `!=` (project extension) |

## 3. Interpretations (not deviations)

These record how a rule is read, so reviewers apply it the same way. None of them lowers a rule.

1. **1.8.c and C11 atomics.** Variables shared between tasks are C11 atomics accessed only through `atomic_*_explicit()` (deviation D-1). An atomic access already stops the compiler from caching, merging or tearing it, which is what 1.8.c's `volatile` is for, and adds the cross-core ordering that `volatile` lacks. Such variables are not also declared `volatile`. Plain variables shared with an ISR or another task, with no atomic access, are `volatile`.
2. **Preferred practices are required here.** 1.7.c, 1.7.d and 6.2.c say "preferred practice"; we treat them as rules, so `goto`, `continue` and early `return` do not appear in files in scope.
3. **File-scope variables are globals for 7.1.j.** Rule 7.2.c speaks of "project- or file-global variables", so `static` variables at file scope also take the `g_` prefix.
4. **Doxygen uses `@` commands** (`@brief`, `@param`), because 2.1.b forbids a backslash inside a comment.
5. **5.2.a is applied strictly.** Every integer in our code is a fixed-width type, even where the width does not matter; only D-2 lines use `int`.
6. **Struct members are not variables.** 7.1 governs variables; members still follow 7.1.e and 7.1.f by review (lowercase, at least 3 characters), and Boolean members take `b_`.
7. **`static` task and ISR functions keep their plan names.** A function that is `static` needs no module prefix (6.1.i covers public functions only), so names from the plan such as `de_task()` and `de_tick_isr()` stay when they are static.

## 4. Deviations

Barr-C's introduction asks for each deviation's approver and reasoning to be documented as close to the deviation as possible. Ours are kept to cases where a rule would break a PostTri commitment or cannot be met with the vendor SDK or the C standard. Each code line that relies on one carries a comment `DEVIATION D-<n>` on that line or on the comment-only line directly above it, and `tools/barr_check.py` accepts a finding only when the ID is in this table and lists the finding's rule. A module-wide deviation is stated once in the file's header comment.

Status: **proposed, not yet cleared with the lecturer.**

| ID | Rule | Reason | PostTri row | Files | Approver |
|---|---|---|---|---|---|
| D-1 | 1.1.a | The seqlock and the fault word need atomic loads, stores, fetch-or, fetch-and and fences with acquire and release ordering across the two ESP32-S3 cores. C99 has no such operations: `volatile` gives no ordering between cores and no atomic read-modify-write. C11 `<stdatomic.h>` provides them, and ESP-IDF v5.3 already compiles C as `gnu17`. Only `<stdatomic.h>` is used beyond C99; no other C11 feature. | Version C row 1 and VC-1 ("relaxed 32-bit atomic halves", "release fence", "acquire fence"); Version B Buddy 3 and change-log row "Set with atomic fetch-or" (LDC-2, VB-3a) | `panel_sync/main/timesync.c`, `timesync.h`, `fault_word.c`, `fault_word.h`, `playback_control.c`, `playback_control.h` (timeline seqlock, added 29 Oct 2026, same reason as timesync; Ahsanul to confirm), `asset_store.c` (published asset pointer and upload phase word, release store and acquire load across the Asset Storage Task and the readers; added 29 Oct 2026, Ahsanul to confirm), their host tests | pending: Ahsanul, then lecturer |
| D-2 | 5.1.a, 5.1.c, 5.2.a, 6.1.e, 6.1.i, 6.3.a | Names, types and macros that ESP-IDF, FreeRTOS, lwIP, the Pico SDK or the C standard define, and signatures they fix: for example `QueueHandle_t`, `pdTRUE`, `pdMS_TO_TICKS()`, `xQueueReceive()`, `esp_err_t`, the `int` socket descriptor and `ssize_t` from lwIP sockets, and `int main (void)`. We cannot rename them, and wrapping each in a Barr-C-named function would add a call layer to the 60 Hz path for no safety gain. Our own names around them follow Barr-C. | none (vendor SDK and C standard) | every file in scope that calls a vendor API; each line that declares a vendor-fixed `int` carries `DEVIATION D-2` | pending: Ahsanul, then lecturer |

Not deviations: `IRAM_ATTR` and the gptimer callback are allowed by 1.1.c (minimum compiler extension, in the driver module `display_engine.c` only); `_Atomic` is covered by D-1.

## 5. Project abbreviations (1.5.b)

| Abbreviation | Meaning |
|---|---|
| `t_p`, `t_l` | Pico time and local tile time of one beacon, microseconds (PostTri interface map row `timesync`) |
| `us`, `ms` | microseconds, milliseconds |
| `ppb`, `ppm` | parts per billion, parts per million |
| `seq` | sequence number |
| `crc` | CRC32 |
| `de`, `tn`, `cl`, `pb` | `display_engine`, `tile_net`, `coordinator_loop`, `playback_control` (static functions only, plan names) |
| `rx`, `tx` | receive, transmit |
| `isr` | interrupt service routine |
