from __future__ import annotations

import datetime

import pytest

import pyspdlog as spd
from pyspdlog import Level


def test_factories_register_loggers(tmp_path):
    log = spd.basic_logger_mt("files", tmp_path / "app.log")
    assert isinstance(log, spd.Logger)
    assert spd.get("files") is log
    assert spd.get("missing") is None


def test_duplicate_names_raise(tmp_path):
    spd.null_logger_mt("dup")
    with pytest.raises(spd.SpdlogError, match="already exists"):
        spd.null_logger_mt("dup")


def test_register_logger(ring):
    log = spd.Logger("manual", ring)
    assert spd.get("manual") is None
    spd.register_logger(log)
    assert spd.get("manual") is log
    with pytest.raises(spd.SpdlogError):
        spd.register_logger(spd.Logger("manual"))
    replacement = spd.Logger("manual")
    spd.register_or_replace(replacement)
    assert spd.get("manual") is replacement


def test_register_logger_rejects_none():
    with pytest.raises(TypeError):
        spd.register_logger(None)


def test_initialize_logger_applies_global_settings(ring):
    spd.set_level(Level.WARN)
    spd.set_pattern("global|%v")
    log = spd.Logger("init", ring)
    spd.initialize_logger(log)
    assert spd.get("init") is log
    assert log.level is Level.WARN
    log.warn("x")
    assert ring.last_formatted() == ["global|x"]


def test_drop_and_drop_all():
    spd.null_logger_mt("a")
    spd.null_logger_mt("b")
    spd.drop("a")
    spd.drop("never-existed")
    assert spd.get("a") is None
    assert spd.get("b") is not None
    spd.drop_all()
    assert spd.all_loggers() == []
    assert spd.default_logger() is None


def test_all_loggers_sorted_by_name():
    spd.drop_all()
    for name in ["zeta", "alpha", "mid"]:
        spd.null_logger_mt(name)
    assert [log.name for log in spd.all_loggers()] == ["alpha", "mid", "zeta"]


def test_automatic_registration_can_be_disabled():
    spd.set_automatic_registration(False)
    log = spd.null_logger_mt("unregistered")
    assert spd.get("unregistered") is None
    spd.initialize_logger(log)
    assert spd.get("unregistered") is None


def test_default_logger_and_module_level_functions(ring):
    log = spd.Logger("main", ring)
    log.set_pattern("%l|%v")
    log.set_level("trace")
    spd.set_default_logger(log)
    assert spd.default_logger() is log
    assert spd.get("main") is log
    assert spd.get_level() is Level.TRACE

    spd.trace("t {}", 1)
    spd.debug("d")
    spd.info("i {name}", name="x")
    spd.warn("w")
    spd.warning("w2")
    spd.error("e")
    spd.critical("c")
    spd.log("info", "l")
    try:
        raise KeyError("k")
    except KeyError:
        spd.exception("ex")
    lines = ring.last_formatted()
    assert lines[:8] == [
        "trace|t 1",
        "debug|d",
        "info|i x",
        "warning|w",
        "warning|w2",
        "error|e",
        "critical|c",
        "info|l",
    ]
    assert lines[8].startswith("error|ex\nTraceback")
    assert spd.should_log("debug")


def test_module_level_functions_without_default_logger():
    spd.set_default_logger(None)
    assert spd.default_logger() is None
    spd.info("goes nowhere")
    spd.exception("also nowhere")
    spd.dump_backtrace()
    assert spd.get_level() is Level.OFF
    assert not spd.should_log(Level.CRITICAL)


def test_global_level_and_pattern(ring):
    log = spd.Logger("g", ring)
    spd.register_logger(log)
    spd.set_level("error")
    assert log.level is Level.ERROR
    spd.set_pattern("G %v")
    log.error("x")
    assert ring.last_formatted() == ["G x"]


def test_global_flush_on():
    flushes = []
    log = spd.Logger("fl", spd.CallbackSink(lambda m: None, flush=lambda: flushes.append(1)))
    spd.register_logger(log)
    spd.flush_on("warn")
    log.warn("x")
    assert flushes == [1]


def test_global_backtrace(ring):
    log = spd.Logger("bt", ring)
    log.set_pattern("%v")
    spd.set_default_logger(log)
    spd.enable_backtrace(5)
    log.debug("hidden")
    spd.dump_backtrace()
    assert "hidden" in ring.last_formatted()
    spd.disable_backtrace()
    assert not log.should_backtrace()


@pytest.mark.parametrize("interval", [0.01, 1, datetime.timedelta(milliseconds=10)])
def test_flush_every_accepts_numbers_and_timedeltas(interval):
    spd.flush_every(interval)
    spd.flush_every(0)


def test_flush_every_flushes_periodically():
    import threading

    flushed = threading.Event()
    log = spd.Logger("periodic", spd.CallbackSink(lambda m: None, flush=flushed.set))
    spd.register_logger(log)
    spd.flush_every(0.01)
    assert flushed.wait(timeout=10)


def test_flush_every_rejects_negative_intervals():
    with pytest.raises(ValueError):
        spd.flush_every(-1)


def test_load_env_levels(monkeypatch):
    a = spd.null_logger_mt("env_a")
    b = spd.null_logger_mt("env_b")
    monkeypatch.setenv("MY_LEVELS", "warn,env_a=trace")
    spd.load_env_levels("MY_LEVELS")
    assert a.level is Level.TRACE
    assert b.level is Level.WARN


def test_file_factories(tmp_path):
    basic = spd.basic_logger_mt("basic", tmp_path / "basic.log", truncate=True)
    rotating = spd.rotating_logger_mt("rotating", tmp_path / "rot.log", 1024, 2)
    daily = spd.daily_logger_mt("daily", tmp_path / "daily.log", hour=3, minute=30)
    for log in (basic, rotating, daily):
        log.set_pattern("%v")
        log.info("written by {}", log.name)
        log.flush()
    assert (tmp_path / "basic.log").read_text().splitlines() == ["written by basic"]
    assert (tmp_path / "rot.log").read_text().splitlines() == ["written by rotating"]
    (daily_file,) = tmp_path.glob("daily_*.log")
    assert daily_file.read_text().splitlines() == ["written by daily"]


def test_console_factories():
    for factory in (spd.stdout_color_mt, spd.stderr_color_mt):
        log = factory(f"{factory.__name__}_logger", spd.ColorMode.NEVER)
        assert spd.get(log.name) is log
    for factory in (spd.stdout_logger_mt, spd.stderr_logger_mt):
        log = factory(f"{factory.__name__}_logger")
        assert spd.get(log.name) is log


def test_asynchronous_factories(tmp_path):
    log = spd.basic_logger_mt("async_file", tmp_path / "a.log", asynchronous=True)
    assert isinstance(log, spd.AsyncLogger)
    log.set_pattern("%v")
    log.info("queued")
    log.flush()
    assert (tmp_path / "a.log").read_text() == "queued\n"
    assert isinstance(spd.stdout_color_mt("async_out", asynchronous=True), spd.AsyncLogger)


def test_asynchronous_is_keyword_only(tmp_path):
    with pytest.raises(TypeError):
        spd.basic_logger_mt("x", tmp_path / "x.log", False, True)
