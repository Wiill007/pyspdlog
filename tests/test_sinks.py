from __future__ import annotations

import datetime
import io
import os
import sys
import threading

import pytest

import pyspdlog as spd
from pyspdlog import Level

from .conftest import run_python


def read_lines(path) -> list[str]:
    with open(path, encoding="utf-8") as f:
        return f.read().splitlines()


def file_logger(sink: spd.Sink, name: str = "file") -> spd.Logger:
    log = spd.Logger(name, sink)
    log.set_pattern("%v")
    return log


class TestBasicFileSink:
    def test_writes_and_appends(self, tmp_path):
        path = tmp_path / "app.log"
        log = file_logger(spd.BasicFileSink(path))
        log.info("first")
        log.flush()
        log = file_logger(spd.BasicFileSink(str(path)))
        log.info("second")
        log.flush()
        assert read_lines(path) == ["first", "second"]

    def test_truncate(self, tmp_path):
        path = tmp_path / "app.log"
        path.write_text("old\n")
        sink = spd.BasicFileSink(path, truncate=True)
        log = file_logger(sink)
        log.info("new")
        log.flush()
        assert read_lines(path) == ["new"]
        sink.truncate()
        log.info("after truncate")
        log.flush()
        assert read_lines(path) == ["after truncate"]

    def test_creates_parent_directories(self, tmp_path):
        path = tmp_path / "a" / "b" / "app.log"
        sink = spd.BasicFileSink(path)
        assert os.path.samefile(sink.filename, path)
        file_logger(sink).info("x")
        sink.flush()
        assert read_lines(path) == ["x"]

    def test_open_failure_raises_spdlog_error(self, tmp_path):
        with pytest.raises(spd.SpdlogError):
            spd.BasicFileSink(tmp_path)  # a directory


class TestRotatingFileSink:
    def test_rotates_by_size(self, tmp_path):
        path = tmp_path / "rot.log"
        sink = spd.RotatingFileSink(path, max_size=100, max_files=2)
        log = file_logger(sink)
        for i in range(30):
            log.info("message number {:02}", i)
        log.flush()
        assert sorted(p.name for p in tmp_path.iterdir()) == ["rot.1.log", "rot.2.log", "rot.log"]
        assert read_lines(path)[-1] == "message number 29"
        assert os.path.samefile(sink.filename, path)

    def test_rotate_now(self, tmp_path):
        path = tmp_path / "rot.log"
        sink = spd.RotatingFileSink(path, max_size=1024, max_files=3)
        log = file_logger(sink)
        log.info("before")
        sink.rotate_now()
        log.info("after")
        log.flush()
        assert read_lines(tmp_path / "rot.1.log") == ["before"]
        assert read_lines(path) == ["after"]

    def test_invalid_size(self, tmp_path):
        with pytest.raises(spd.SpdlogError):
            spd.RotatingFileSink(tmp_path / "x.log", max_size=0, max_files=1)


class TestDailyFileSink:
    def test_adds_date_to_file_name(self, tmp_path):
        sink = spd.DailyFileSink(tmp_path / "daily.log", rotation_hour=0, rotation_minute=0)
        file_logger(sink).info("today")
        sink.flush()
        today = datetime.date.today().isoformat()
        expected = tmp_path / f"daily_{today}.log"
        assert os.path.samefile(sink.filename, expected)
        assert read_lines(expected) == ["today"]

    def test_invalid_rotation_time(self, tmp_path):
        with pytest.raises(spd.SpdlogError):
            spd.DailyFileSink(tmp_path / "daily.log", rotation_hour=24)


def test_null_sink():
    log = spd.Logger("null", spd.NullSink())
    log.critical("gone")
    log.flush()


class TestRingbufferSink:
    def test_keeps_newest_messages(self):
        sink = spd.RingbufferSink(3)
        log = file_logger(sink)
        for i in range(5):
            log.info("{}", i)
        assert sink.last_formatted() == ["2", "3", "4"]
        assert sink.last_formatted(limit=2) == ["3", "4"]

    def test_default_pattern_is_full(self):
        sink = spd.RingbufferSink(1)
        spd.Logger("rb", sink).info("hello")
        (line,) = sink.last_formatted()
        assert line.endswith("[rb] [info] hello")


class TestCallbackSink:
    def test_receives_log_messages(self):
        messages: list[spd.LogMessage] = []
        sink = spd.CallbackSink(messages.append)
        sink.set_pattern("%n|%l|%v")
        log = spd.Logger("cb", sink)
        before = datetime.datetime.now()
        log.warning("hello {}", "there")
        after = datetime.datetime.now()

        (msg,) = messages
        assert msg.logger_name == "cb"
        assert msg.level is Level.WARN
        assert msg.payload == "hello there"
        assert msg.formatted == "cb|warning|hello there"
        assert before - datetime.timedelta(seconds=1) <= msg.time <= after + datetime.timedelta(seconds=1)
        assert msg.thread_id == threading.get_native_id()
        assert repr(msg) == "LogMessage(logger_name='cb', level=warning, payload='hello there')"
        assert sink.callback == messages.append

    def test_flush_callback(self):
        flushed = []
        sink = spd.CallbackSink(lambda m: None, flush=lambda: flushed.append(True))
        sink.flush()
        assert flushed == [True]
        spd.CallbackSink(lambda m: None).flush()  # no flush callback: nothing happens

    def test_rejects_non_callables(self):
        with pytest.raises(TypeError):
            spd.CallbackSink("print")
        with pytest.raises(TypeError):
            spd.CallbackSink(print, flush=42)

    def test_callback_errors_are_unraisable(self, unraisable):
        def broken(msg):
            raise RuntimeError(f"cannot handle {msg.payload}")

        log = spd.Logger("broken", spd.CallbackSink(broken))
        log.info("this")  # must not raise
        (exc,) = unraisable
        assert isinstance(exc, RuntimeError)
        assert str(exc) == "cannot handle this"

    def test_callback_can_log_to_the_same_logger(self):
        payloads = []

        def callback(msg):
            payloads.append(msg.payload)
            if msg.payload == "outer":
                log.info("inner")

        log = spd.Logger("reentrant", spd.CallbackSink(callback))
        log.info("outer")
        assert payloads == ["outer", "inner"]


class TestStreamSink:
    def test_writes_lines_to_a_stream(self):
        stream = io.StringIO()
        sink = spd.StreamSink(stream)
        sink.set_pattern("[%l] %v")
        log = spd.Logger("stream", sink)
        log.info("one")
        log.error("two")
        assert stream.getvalue() == "[info] one\n[error] two\n"
        assert sink.stream is stream

    def test_defaults_to_current_sys_stdout(self, capsys):
        sink = spd.StreamSink()
        sink.set_pattern("%v")
        spd.Logger("stdout", sink).info("captured")
        assert capsys.readouterr().out == "captured\n"
        assert sink.stream is None

    def test_flush_flushes_the_stream(self):
        class Stream(io.StringIO):
            flushes = 0

            def flush(self):
                Stream.flushes += 1

        sink = spd.StreamSink(Stream())
        sink.flush()
        assert Stream.flushes == 1

    def test_write_errors_are_unraisable(self, unraisable):
        stream = io.StringIO()
        stream.close()
        spd.Logger("closed", spd.StreamSink(stream)).info("x")
        (exc,) = unraisable
        assert isinstance(exc, ValueError)


CONSOLE_SCRIPT = """
import pyspdlog as spd
for sink in [spd.{cls}({args})]:
    log = spd.Logger("console", sink)
    log.set_pattern("%^%l%$|%v")
    log.info("hello")
"""


@pytest.mark.parametrize(
    ("cls", "args", "stream"),
    [
        ("StdoutSink", "", "stdout"),
        ("StderrSink", "", "stderr"),
        ("StdoutColorSink", "spd.ColorMode.NEVER", "stdout"),
        ("StderrColorSink", "spd.ColorMode.NEVER", "stderr"),
    ],
)
def test_console_sinks(cls, args, stream):
    result = run_python(CONSOLE_SCRIPT.format(cls=cls, args=args))
    assert result.returncode == 0, result.stderr
    assert getattr(result, stream).splitlines() == ["info|hello"]


@pytest.mark.skipif(sys.platform == "win32", reason="Windows color sinks use console attributes")
def test_color_sink_always_emits_ansi_codes():
    result = run_python(CONSOLE_SCRIPT.format(cls="StdoutColorSink", args="spd.ColorMode.ALWAYS"))
    assert result.returncode == 0, result.stderr
    assert "\x1b[" in result.stdout
    assert "hello" in result.stdout
