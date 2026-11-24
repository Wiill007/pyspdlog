from __future__ import annotations

import logging

import pytest

import pyspdlog as spd
from pyspdlog import Level, SpdlogHandler, level_from_logging


@pytest.fixture
def py_logger():
    log = logging.getLogger("pyspdlog-tests")
    log.setLevel(logging.DEBUG)
    log.propagate = False
    yield log
    for handler in list(log.handlers):
        log.removeHandler(handler)


@pytest.mark.parametrize(
    ("levelno", "expected"),
    [
        (logging.CRITICAL, Level.CRITICAL),
        (logging.ERROR, Level.ERROR),
        (logging.WARNING, Level.WARN),
        (25, Level.INFO),
        (logging.INFO, Level.INFO),
        (logging.DEBUG, Level.DEBUG),
        (5, Level.TRACE),
        (logging.NOTSET, Level.TRACE),
    ],
)
def test_level_mapping(levelno, expected):
    assert level_from_logging(levelno) is expected


def test_forwards_records(py_logger, logger, ring):
    py_logger.addHandler(SpdlogHandler(logger))
    py_logger.debug("debug %s", "msg")
    py_logger.info("info")
    py_logger.warning("warning")
    py_logger.error("error")
    py_logger.critical("critical")
    assert ring.last_formatted() == [
        "debug|debug msg",
        "info|info",
        "warning|warning",
        "error|error",
        "critical|critical",
    ]


def test_message_braces_are_not_formatted_again(py_logger, logger, ring):
    py_logger.addHandler(SpdlogHandler(logger))
    py_logger.info("dict: %s", {"a": 1})
    assert ring.last_formatted() == ["info|dict: {'a': 1}"]


def test_uses_the_handler_formatter(py_logger, logger, ring):
    handler = SpdlogHandler(logger)
    handler.setFormatter(logging.Formatter("%(name)s: %(message)s"))
    py_logger.addHandler(handler)
    py_logger.info("hi")
    assert ring.last_formatted() == ["info|pyspdlog-tests: hi"]


def test_exc_info_adds_traceback(py_logger, logger, ring):
    py_logger.addHandler(SpdlogHandler(logger))
    try:
        1 / 0
    except ZeroDivisionError:
        py_logger.exception("failed")
    (line,) = ring.last_formatted()
    assert line.startswith("error|failed\nTraceback")
    assert line.endswith("ZeroDivisionError: division by zero")


def test_respects_spdlog_level(py_logger, logger, ring):
    logger.set_level(Level.WARN)
    py_logger.addHandler(SpdlogHandler(logger))
    py_logger.info("dropped")
    py_logger.warning("kept")
    assert ring.last_formatted() == ["warning|kept"]


def test_handler_level(py_logger, logger, ring):
    py_logger.addHandler(SpdlogHandler(logger, level=logging.ERROR))
    py_logger.warning("dropped")
    py_logger.error("kept")
    assert ring.last_formatted() == ["error|kept"]


def test_target_by_name(py_logger, ring):
    handler = SpdlogHandler("named")
    py_logger.addHandler(handler)
    py_logger.info("before the logger exists")
    assert handler.target is None
    log = spd.Logger("named", ring)
    log.set_pattern("%v")
    spd.register_logger(log)
    py_logger.info("after")
    assert handler.target is log
    assert ring.last_formatted() == ["after"]


def test_default_target(py_logger, ring):
    log = spd.Logger("default", ring)
    log.set_pattern("%v")
    spd.set_default_logger(log)
    handler = SpdlogHandler()
    py_logger.addHandler(handler)
    py_logger.info("to default")
    assert handler.target is log
    assert ring.last_formatted() == ["to default"]
    spd.set_default_logger(None)
    py_logger.info("dropped silently")


def test_flush(py_logger):
    flushes = []
    handler = SpdlogHandler(spd.Logger("f", spd.CallbackSink(lambda m: None, flush=lambda: flushes.append(1))))
    handler.flush()
    assert flushes == [1]
    SpdlogHandler("missing").flush()


def test_repr(logger):
    assert repr(SpdlogHandler(logger)) == "<SpdlogHandler 'test' (NOTSET)>"
    assert repr(SpdlogHandler("x", logging.INFO)) == "<SpdlogHandler 'x' (INFO)>"
    assert repr(SpdlogHandler()) == "<SpdlogHandler '<default>' (NOTSET)>"


def test_errors_go_to_handle_error(logger, monkeypatch):
    handler = SpdlogHandler(logger)
    errors = []
    monkeypatch.setattr(handler, "handleError", errors.append)
    # Emitted directly: pytest's own log capture handler would re-raise the error
    record = logging.LogRecord("x", logging.INFO, __file__, 1, "%d", ("not a number",), None)
    handler.handle(record)
    assert errors == [record]
