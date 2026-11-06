"""Run the C-side cross-language tests where pytest is missing (laptop WSL).

On the Windows laptop, pytest runs on Windows Python, which has no make or
gcc, so pc/tests/test_cross_lang.py skips its two C-side tests. WSL has make
and gcc but no pytest. This runner imports the unchanged test module with a
minimal stand-in for the pytest names it uses, builds the C tool with make,
and calls every test in the module except WINDOWS_ONLY. It is no substitute for pytest; CI still
runs the real thing.

    python3 scripts/wsl_cross_lang.py      (inside WSL, from the repo root)

Exit code 0 when every test passes.
"""

import os
import subprocess
import sys
import tempfile
import traceback
import types

ROOT = os.path.normpath(os.path.join(os.path.dirname(__file__), ".."))

# Left to the Windows pytest run, where it passes: with core.autocrlf=true the
# checkout has CRLF, and WSL Python writes LF, so the byte compare fails here
# on line endings alone.
WINDOWS_ONLY = {"test_committed_vectors_are_current"}


class _Skip(Exception):
    """Raised by the stand-in pytest.skip()."""


def _stub_pytest() -> None:
    stub = types.ModuleType("pytest")

    def fixture(*args, **kwargs):  # type: ignore
        if args and callable(args[0]):
            return args[0]
        return lambda fn: fn

    def skip(reason: str) -> None:
        raise _Skip(reason)

    stub.fixture = fixture  # type: ignore[attr-defined]
    stub.skip = skip  # type: ignore[attr-defined]
    sys.modules["pytest"] = stub


def main() -> int:
    _stub_pytest()
    sys.path.insert(0, os.path.join(ROOT, "pc", "tests"))
    import test_cross_lang as tcl  # noqa: E402

    tool = tcl.tool()
    failed = 0
    names = sorted(n for n in dir(tcl)
                   if n.startswith("test_") and n not in WINDOWS_ONLY)
    for name in names:
        fn = getattr(tcl, name)
        params = fn.__code__.co_varnames[:fn.__code__.co_argcount]
        try:
            with tempfile.TemporaryDirectory() as tmp:
                args = []
                for param in params:
                    if param == "tool":
                        args.append(tool)
                    elif param == "tmp_path":
                        args.append(tmp)
                    else:
                        raise RuntimeError("unknown fixture " + param)
                fn(*args)
            print("PASS " + name)
        except _Skip as exc:
            print("SKIP %s: %s" % (name, exc))
            failed += 1
        except Exception:  # noqa: BLE001
            print("FAIL " + name)
            traceback.print_exc()
            failed += 1
    subprocess.run(["make", "-s", "-C", tcl.TEST_DIR, "clean"],
                   check=False, capture_output=True)
    print("cross_lang: %d of %d passed" % (len(names) - failed, len(names)))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
