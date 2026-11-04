"""pytest suite for pc/tools/serial_log.py (no serial port needed)."""

from __future__ import annotations

import io
import os
import sys
from datetime import datetime
from typing import List

import pytest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
import serial_log as sl  # noqa: E402


def fake_clock(*moments: datetime):
    """A clock that returns the given moments in turn."""
    it = iter(moments)
    return lambda: next(it)


def at(second: int, micro: int = 0) -> datetime:
    return datetime(2026, 11, 5, 9, 30, second, micro)


def test_header_format_with_all_values() -> None:
    header = sl.make_header("D1", "2026-11-05", "bench-5nov", "3/9c:13:aa",
                            "tile_plain_A", "A", "B", "jolyn", "COM5")
    assert header == ("# test=D1 date=2026-11-05 tag=bench-5nov "
                      "tile=3/9c:13:aa image=tile_plain_A net=A pico=B "
                      "who=jolyn port=COM5")


def test_header_missing_optionals_are_dashes() -> None:
    header = sl.make_header("D1", "2026-11-05", "bench-5nov", None, None,
                            None, None, "jolyn", "COM5")
    assert header == ("# test=D1 date=2026-11-05 tag=bench-5nov tile=- "
                      "image=- net=- pico=- who=jolyn port=COM5")


def test_stamp_has_millisecond_precision() -> None:
    assert sl.format_stamp(at(7, 5_999)) == "09:30:07.005"
    assert sl.format_stamp(at(7, 123_456)) == "09:30:07.123"
    assert sl.format_stamp(datetime(2026, 1, 1, 0, 0, 0)) == "00:00:00.000"


def test_lines_get_a_stamp_from_the_clock() -> None:
    out = list(sl.log_lines([b"boot\nready\n"], fake_clock(at(1, 2_000),
                                                           at(2, 500_000))))
    assert out == ["09:30:01.002 boot", "09:30:02.500 ready"]


def test_split_chunks_are_joined() -> None:
    chunks = [b"hel", b"lo wor", b"ld\nsec", b"ond\n"]
    assert list(sl.iter_lines(chunks)) == ["hello world", "second"]


def test_crlf_is_removed_even_when_split() -> None:
    chunks = [b"one\r\ntwo\r", b"\nthree\r\n"]
    assert list(sl.iter_lines(chunks)) == ["one", "two", "three"]


def test_empty_lines_are_kept_and_empty_chunks_ignored() -> None:
    assert list(sl.iter_lines([b"a\n", b"", b"\n", b"b\n"])) == ["a", "", "b"]


def test_invalid_utf8_is_replaced_not_fatal() -> None:
    out = list(sl.iter_lines([b"ok \xff\xfe bad\n"]))
    assert out == ["ok �� bad"]


def test_multibyte_character_split_across_chunks() -> None:
    data = "café\n".encode("utf-8")
    assert list(sl.iter_lines([data[:4], data[4:]])) == ["café"]


def test_partial_line_without_newline_is_kept_at_the_end() -> None:
    assert list(sl.iter_lines([b"done\npartial"])) == ["done", "partial"]


def test_clock_is_read_when_the_line_completes() -> None:
    calls: List[int] = []

    def clock() -> datetime:
        calls.append(1)
        return at(len(calls))

    gen = sl.log_lines([b"a\n", b"b"], clock)
    assert next(gen) == "09:30:01.000 a"
    assert len(calls) == 1


def test_run_log_writes_flushes_echoes_and_counts() -> None:
    class Flushing(io.StringIO):
        flushes = 0

        def flush(self) -> None:
            Flushing.flushes += 1
            super().flush()

    out, echo = Flushing(), io.StringIO()
    count = sl.run_log([b"x\ny\n"], out, echo,
                       fake_clock(at(1), at(2)))
    assert count == 2
    assert out.getvalue() == echo.getvalue() == (
        "09:30:01.000 x\n09:30:02.000 y\n")
    assert Flushing.flushes >= 2


class FakePort:
    """Stands in for serial.Serial: replays chunks, then times out."""

    def __init__(self, chunks):
        self.chunks = list(chunks)
        self.closed = False

    def read(self, size: int) -> bytes:
        return self.chunks.pop(0) if self.chunks else b""

    def close(self) -> None:
        self.closed = True


def test_main_appends_header_lines_and_end(tmp_path, monkeypatch,
                                           capsys) -> None:
    out = tmp_path / "sub" / "run.log"
    port = FakePort([b"boot 1\r\nlin", b"e 2\r\n"])
    monkeypatch.setattr(sl, "read_chunks",
                        lambda p, b, s: iter(port.chunks))
    for boot in range(2):
        port.chunks = [b"boot 1\r\nlin", b"e 2\r\n"]
        rc = sl.main(["--port", "COM5", "--out", str(out), "--test", "D1",
                      "--who", "jolyn", "--minutes", "0.01"])
        assert rc == 0
    lines = out.read_text(encoding="utf-8").splitlines()
    assert lines[0].startswith("# test=D1 date=")
    assert " tag=bench-5nov tile=- image=- net=- pico=- who=jolyn " \
           "port=COM5" in lines[0]
    assert lines[1][13:] == "boot 1" and lines[2][13:] == "line 2"
    assert lines[3] == "# end lines=2"
    assert lines[4] == lines[0]  # second boot appended to the same file
    assert lines[-1] == "# end lines=2"
    assert len(lines) == 8
    assert "boot 1" in capsys.readouterr().out


def test_read_chunks_needs_pyserial_only_when_called(monkeypatch) -> None:
    monkeypatch.setitem(sys.modules, "serial", None)  # import now fails
    with pytest.raises(ImportError):
        next(sl.read_chunks("COM5", 115200, 1))


def test_read_chunks_uses_timeout_and_stops_after_time(monkeypatch) -> None:
    opened = {}
    port = FakePort([b"a\n", b"b\n"])

    class FakeSerialModule:
        @staticmethod
        def Serial(name, baud, timeout):
            opened.update(name=name, baud=baud, timeout=timeout)
            return port

    monkeypatch.setitem(sys.modules, "serial", FakeSerialModule)
    got = list(sl.read_chunks("COM5", 9600, 0.05))
    assert got == [b"a\n", b"b\n"]
    assert opened == {"name": "COM5", "baud": 9600, "timeout": 0.2}
    assert port.closed
