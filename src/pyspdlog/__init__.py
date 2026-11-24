"""Fast C++ logging for Python, powered by spdlog and nanobind.

Quick start::

    import pyspdlog

    pyspdlog.info("hello {}", "world")

    log = pyspdlog.stdout_color_mt("app")
    log.set_level("debug")
    log.debug("only formatted when DEBUG is enabled: {}", 42)
"""

from __future__ import annotations

import atexit
import os
from importlib import metadata as _metadata

from ._core import (
    SPDLOG_VERSION,
    AsyncLogger,
    BasicFileSink,
    CallbackSink,
    ColorMode,
    DailyFileSink,
    Level,
    Logger,
    LogMessage,
    NullSink,
    OverflowPolicy,
    PatternTimeType,
    RingbufferSink,
    RotatingFileSink,
    Sink,
    SpdlogError,
    StderrColorSink,
    StderrSink,
    StdoutColorSink,
    StdoutSink,
    StreamSink,
    all_loggers,
    basic_logger_mt,
    critical,
    daily_logger_mt,
    debug,
    default_logger,
    disable_backtrace,
    drop,
    drop_all,
    dump_backtrace,
    enable_backtrace,
    error,
    exception,
    flush_every,
    flush_on,
    get,
    get_level,
    info,
    init_thread_pool,
    initialize_logger,
    level_from_str,
    load_env_levels,
    log,
    null_logger_mt,
    register_logger,
    register_or_replace,
    rotating_logger_mt,
    set_automatic_registration,
    set_default_logger,
    set_level,
    set_pattern,
    should_log,
    shutdown,
    stderr_color_mt,
    stderr_logger_mt,
    stdout_color_mt,
    stdout_logger_mt,
    trace,
    warn,
    warning,
)
from ._core import _after_fork_in_child, _after_fork_in_parent, _before_fork, _release_python_sinks
from .handler import SpdlogHandler, level_from_logging

try:
    __version__ = _metadata.version("pyspdlog")
except _metadata.PackageNotFoundError:  # imported from a source tree
    __version__ = "0.0.0+unknown"


def _shutdown_at_exit() -> None:
    # The async thread pool and the periodic flusher run sinks on their own
    # threads, and Python-backed sinks call into the interpreter from there:
    # stop them (flushing every logger) while the interpreter is fully alive.
    shutdown()
    # Python-backed sinks can be part of reference cycles that pass through
    # C++ (callback -> module globals -> logger -> sink -> callback), which the
    # garbage collector cannot see. Drop their references so everything is
    # freed during interpreter shutdown.
    _release_python_sinks()


atexit.register(_shutdown_at_exit)

# Background threads do not survive fork(). In the child, the inherited thread
# pool and periodic flusher are abandoned (joining them would hang) and async
# loggers created before the fork drop their messages with a RuntimeWarning.
if hasattr(os, "register_at_fork"):
    os.register_at_fork(
        before=_before_fork,
        after_in_parent=_after_fork_in_parent,
        after_in_child=_after_fork_in_child,
    )

__all__ = [
    "SPDLOG_VERSION",
    "AsyncLogger",
    "BasicFileSink",
    "CallbackSink",
    "ColorMode",
    "DailyFileSink",
    "Level",
    "LogMessage",
    "Logger",
    "NullSink",
    "OverflowPolicy",
    "PatternTimeType",
    "RingbufferSink",
    "RotatingFileSink",
    "Sink",
    "SpdlogError",
    "SpdlogHandler",
    "StderrColorSink",
    "StderrSink",
    "StdoutColorSink",
    "StdoutSink",
    "StreamSink",
    "__version__",
    "all_loggers",
    "basic_logger_mt",
    "critical",
    "daily_logger_mt",
    "debug",
    "default_logger",
    "disable_backtrace",
    "drop",
    "drop_all",
    "dump_backtrace",
    "enable_backtrace",
    "error",
    "exception",
    "flush_every",
    "flush_on",
    "get",
    "get_level",
    "info",
    "init_thread_pool",
    "initialize_logger",
    "level_from_logging",
    "level_from_str",
    "load_env_levels",
    "log",
    "null_logger_mt",
    "register_logger",
    "register_or_replace",
    "rotating_logger_mt",
    "set_automatic_registration",
    "set_default_logger",
    "set_level",
    "set_pattern",
    "should_log",
    "shutdown",
    "stderr_color_mt",
    "stderr_logger_mt",
    "stdout_color_mt",
    "stdout_logger_mt",
    "trace",
    "warn",
    "warning",
]
