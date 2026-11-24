from __future__ import annotations

import enum

import pytest

import pyspdlog as spd
from pyspdlog import Level


def test_levels_are_ordered_int_enum():
    assert issubclass(Level, enum.IntEnum)
    assert [lvl.name for lvl in Level] == ["TRACE", "DEBUG", "INFO", "WARN", "ERROR", "CRITICAL", "OFF"]
    assert Level.TRACE < Level.DEBUG < Level.INFO < Level.WARN < Level.ERROR < Level.CRITICAL < Level.OFF


@pytest.mark.parametrize(
    ("name", "expected"),
    [
        ("trace", Level.TRACE),
        ("DEBUG", Level.DEBUG),
        ("Info", Level.INFO),
        ("warn", Level.WARN),
        ("warning", Level.WARN),
        ("err", Level.ERROR),
        ("error", Level.ERROR),
        ("critical", Level.CRITICAL),
        ("off", Level.OFF),
    ],
)
def test_level_from_str(name, expected):
    assert spd.level_from_str(name) is expected


def test_level_from_str_rejects_unknown_names():
    with pytest.raises(ValueError, match="unknown log level 'loud'"):
        spd.level_from_str("loud")


def test_level_arguments_accept_names(logger, ring):
    logger.set_level("warning")
    assert logger.level is Level.WARN
    logger.level = "debug"
    assert logger.level is Level.DEBUG
    logger.log("error", "by name")
    assert ring.last_formatted() == ["error|by name"]
    assert logger.should_log("info")
    assert not logger.should_log(Level.TRACE)


def test_level_arguments_reject_unknown_names(logger):
    with pytest.raises(ValueError, match="unknown log level"):
        logger.set_level("verbose")
    with pytest.raises(ValueError, match="unknown log level"):
        logger.log("verbose", "x")


def test_level_arguments_reject_other_types(logger):
    with pytest.raises(TypeError):
        logger.set_level(3.5)


def test_logger_level_filters(logger, ring):
    logger.set_level(Level.WARN)
    logger.info("dropped")
    logger.warn("kept")
    logger.critical("kept too")
    assert ring.last_formatted() == ["warning|kept", "critical|kept too"]


def test_off_disables_everything(logger, ring):
    logger.set_level(Level.OFF)
    logger.critical("nope")
    assert ring.last_formatted() == []


def test_sink_level_filters_independently(ring):
    errors = spd.RingbufferSink(10)
    errors.level = "error"
    log = spd.Logger("multi", [ring, errors])
    log.set_pattern("%v")
    log.info("info")
    log.error("error")
    assert ring.last_formatted() == ["info", "error"]
    assert errors.last_formatted() == ["error"]
    assert errors.level is Level.ERROR
    assert errors.should_log("critical")
    assert not errors.should_log(Level.WARN)
