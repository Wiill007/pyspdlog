from __future__ import annotations

import threading

import pytest

import pyspdlog as spd
from pyspdlog import Level


class CountingFormat:
    """Counts how often it is formatted, to check that formatting is lazy."""

    def __init__(self) -> None:
        self.calls = 0

    def __format__(self, spec: str) -> str:
        self.calls += 1
        return "formatted"


def test_level_methods(logger, ring):
    logger.trace("t")
    logger.debug("d")
    logger.info("i")
    logger.warn("w")
    logger.warning("w2")
    logger.error("e")
    logger.critical("c")
    logger.log(Level.INFO, "l")
    assert ring.last_formatted() == [
        "trace|t",
        "debug|d",
        "info|i",
        "warning|w",
        "warning|w2",
        "error|e",
        "critical|c",
        "info|l",
    ]


def test_str_format_arguments(logger, ring):
    logger.info("{} + {} = {total}", 1, 2, total=3)
    logger.info("{0:>5}|{1:.2f}", "ab", 3.14159)
    assert ring.last_formatted() == ["info|1 + 2 = 3", "info|   ab|3.14"]


def test_keyword_arguments_named_like_parameters(logger, ring):
    logger.info("{level} {msg} {args}", level=80, msg="m", args="a")
    logger.log(Level.INFO, "{level}", level="x")
    assert ring.last_formatted() == ["info|80 m a", "info|x"]


def test_message_without_arguments_is_not_formatted(logger, ring):
    logger.info("literal {} and {name}")
    assert ring.last_formatted() == ["info|literal {} and {name}"]


def test_non_string_messages_use_str(logger, ring):
    class Thing:
        def __str__(self) -> str:
            return "a thing"

    logger.info(42)
    logger.info(Thing())
    logger.info(None)
    logger.info(b"bytes")
    logger.log("info", None)
    assert ring.last_formatted() == ["info|42", "info|a thing", "info|None", "info|b'bytes'", "info|None"]


def test_message_is_required(logger):
    with pytest.raises(TypeError):
        logger.info()
    with pytest.raises(TypeError):
        logger.info(msg="keyword messages are not supported")


def test_unicode_round_trip(logger, ring):
    logger.info("héllo {} 🚀 日本", "wörld")
    assert ring.last_formatted() == ["info|héllo wörld 🚀 日本"]


def test_formatting_is_lazy(logger):
    value = CountingFormat()
    logger.set_level(Level.INFO)
    logger.debug("{}", value)
    assert value.calls == 0
    logger.info("{}", value)
    assert value.calls == 1


def test_formatting_errors_propagate(logger):
    with pytest.raises(IndexError):
        logger.info("{} {}", 1)
    with pytest.raises(KeyError):
        logger.info("{missing}", 1)


def test_exception_adds_traceback(logger, ring):
    try:
        raise ValueError("bad value")
    except ValueError:
        logger.exception("failed to {}", "parse")
    (line,) = ring.last_formatted()
    first, *rest = line.splitlines()
    assert first == "error|failed to parse"
    assert rest[0] == "Traceback (most recent call last):"
    assert rest[-1] == "ValueError: bad value"


def test_exception_outside_except_block(logger, ring):
    logger.exception("no active exception")
    assert ring.last_formatted() == ["error|no active exception"]


def test_name_repr_and_sinks(ring):
    log = spd.Logger("app.db", [ring])
    assert log.name == "app.db"
    assert repr(log) == "<Logger 'app.db' level=info>"
    assert log.sinks == [ring]
    assert log.sinks[0] is ring


def test_logger_without_sinks_is_a_no_op():
    log = spd.Logger("empty")
    log.info("nowhere")
    assert log.sinks == []


def test_logger_constructors_reject_bad_sinks():
    with pytest.raises(TypeError):
        spd.Logger("bad", ["not a sink"])
    with pytest.raises(TypeError):
        spd.Logger("bad", None)


def test_set_pattern_applies_to_all_sinks(ring):
    other = spd.RingbufferSink(10)
    log = spd.Logger("p", [ring, other])
    log.set_pattern("<%n> %v")
    log.info("x")
    assert ring.last_formatted() == ["<p> x"]
    assert other.last_formatted() == ["<p> x"]


def test_utc_pattern_time(ring):
    log = spd.Logger("utc", ring)
    log.set_pattern("%z", spd.PatternTimeType.UTC)
    log.info("x")
    assert ring.last_formatted() == ["+00:00"]


def test_clone_shares_sinks_and_is_not_registered(logger, ring):
    copy = logger.clone("copy")
    assert copy.name == "copy"
    assert copy.level is logger.level
    assert copy.sinks[0] is ring
    copy.info("from clone")
    assert ring.last_formatted() == ["info|from clone"]
    assert spd.get("copy") is None


def test_backtrace(logger, ring):
    logger.set_level(Level.INFO)
    logger.enable_backtrace(2)
    assert logger.should_backtrace()
    logger.debug("one")
    logger.debug("two {}", 2)
    logger.debug("three")
    assert ring.last_formatted() == []
    logger.dump_backtrace()
    lines = ring.last_formatted()
    assert lines[1:3] == ["debug|two 2", "debug|three"]
    assert "Backtrace" in lines[0] and "Backtrace" in lines[-1]
    logger.disable_backtrace()
    assert not logger.should_backtrace()


def test_flush_on(logger):
    flushes = []
    sink = spd.CallbackSink(lambda msg: None, flush=lambda: flushes.append(1))
    log = spd.Logger("f", sink)
    assert log.flush_level is Level.OFF
    log.flush_on("error")
    assert log.flush_level is Level.ERROR
    log.warn("no flush")
    assert flushes == []
    log.error("flush")
    assert flushes == [1]
    log.flush()
    assert flushes == [1, 1]


def test_concurrent_logging_from_python_threads():
    ring = spd.RingbufferSink(8 * 200)
    log = spd.Logger("threads", ring)
    log.set_pattern("%v")

    def work(n: int) -> None:
        for i in range(200):
            log.info("{}-{}", n, i)

    threads = [threading.Thread(target=work, args=(n,)) for n in range(8)]
    for t in threads:
        t.start()
    for t in threads:
        t.join()
    lines = ring.last_formatted()
    assert len(lines) == 8 * 200
    assert set(lines) == {f"{n}-{i}" for n in range(8) for i in range(200)}
