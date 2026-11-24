"""Bridge from the standard :mod:`logging` module to spdlog."""

from __future__ import annotations

import logging

from ._core import Level, Logger, default_logger, get

__all__ = ["SpdlogHandler", "level_from_logging"]


def level_from_logging(levelno: int) -> Level:
    """Map a :mod:`logging` level number to the closest spdlog :class:`Level`.

    Levels between the standard ones round down, so a custom level 25 maps to
    ``Level.INFO`` and anything below ``logging.DEBUG`` maps to ``Level.TRACE``.
    """
    if levelno >= logging.CRITICAL:
        return Level.CRITICAL
    if levelno >= logging.ERROR:
        return Level.ERROR
    if levelno >= logging.WARNING:
        return Level.WARN
    if levelno >= logging.INFO:
        return Level.INFO
    if levelno >= logging.DEBUG:
        return Level.DEBUG
    return Level.TRACE


class SpdlogHandler(logging.Handler):
    """A :class:`logging.Handler` that sends records to a spdlog logger.

    ``logger`` can be a :class:`Logger`, the name of a registered logger, or
    ``None`` for the default logger. Names and the default logger are looked up
    on every record, so they can be replaced while the handler is in use.

    The record is rendered with the handler's :class:`logging.Formatter`
    (``"%(message)s"`` plus any traceback by default) and that text becomes the
    spdlog message, so spdlog's pattern adds the time, level and so on::

        logging.basicConfig(
            level=logging.INFO,
            format="%(name)s: %(message)s",  # basicConfig's default repeats the level
            handlers=[SpdlogHandler()],
        )
    """

    def __init__(self, logger: Logger | str | None = None, level: int | str = logging.NOTSET) -> None:
        super().__init__(level)
        self._target = logger

    @property
    def target(self) -> Logger | None:
        """The spdlog logger records currently go to, or ``None`` if there is none."""
        target = self._target
        if target is None:
            return default_logger()
        if isinstance(target, str):
            return get(target)
        return target

    def emit(self, record: logging.LogRecord) -> None:
        try:
            target = self.target
            if target is None:
                return
            level = level_from_logging(record.levelno)
            if not target.should_log(level) and not target.should_backtrace():
                return
            target.log(level, self.format(record))
        except RecursionError:  # same policy as logging.StreamHandler
            raise
        except Exception:
            self.handleError(record)

    def flush(self) -> None:
        target = self.target
        if target is not None:
            target.flush()

    def __repr__(self) -> str:
        target = self._target
        name = "<default>" if target is None else target if isinstance(target, str) else target.name
        return f"<{type(self).__name__} {name!r} ({logging.getLevelName(self.level)})>"
