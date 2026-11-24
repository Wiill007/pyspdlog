"""Compare pyspdlog with the standard logging module.

Run with:  uv run python benchmarks/bench.py
"""

from __future__ import annotations

import logging
import os
import tempfile
import timeit

import pyspdlog as spd

N = 200_000


def per_call_ns(stmt: str, log, number: int = N, repeat: int = 5) -> float:
    """Best per-call time of `stmt`, a statement using `log`."""
    times = timeit.repeat(stmt, globals={"log": log}, number=number, repeat=repeat)
    return min(times) / number * 1e9


PY_CALL = 'log.{}("value %s %s", 1, "x")'
SP_CALL = 'log.{}("value {{}} {{}}", 1, "x")'


def stdlib_logger(name: str, handler: logging.Handler) -> logging.Logger:
    log = logging.getLogger(name)
    log.handlers[:] = [handler]
    log.propagate = False
    log.setLevel(logging.INFO)
    handler.setFormatter(logging.Formatter("[%(asctime)s] [%(name)s] [%(levelname)s] %(message)s"))
    return log


def main() -> None:
    rows: list[tuple[str, float, float]] = []

    with tempfile.TemporaryDirectory() as tmp:
        # A disabled level: the cost of a debug() call that is filtered out
        py = stdlib_logger("bench.disabled", logging.NullHandler())
        sp = spd.Logger("bench.disabled", spd.NullSink())
        rows.append((
            "disabled level, with args",
            per_call_ns(PY_CALL.format("debug"), py),
            per_call_ns(SP_CALL.format("debug"), sp),
        ))

        # Formatting a record and throwing it away
        py = stdlib_logger("bench.null", logging.NullHandler())
        py_handler = py.handlers[0]
        py_handler.emit = lambda record: py_handler.format(record)  # format, don't write
        sp = spd.Logger("bench.null", spd.NullSink())
        rows.append((
            "format and discard",
            per_call_ns(PY_CALL.format("info"), py),
            per_call_ns(SP_CALL.format("info"), sp),
        ))

        # Writing to a file. logging.FileHandler flushes after every record;
        # spdlog buffers unless told to flush (flush_on / flush_every).
        py = stdlib_logger("bench.file", logging.FileHandler(os.path.join(tmp, "py.log")))
        sp = spd.Logger("bench.file", spd.BasicFileSink(os.path.join(tmp, "sp.log")))
        py_ns = per_call_ns(PY_CALL.format("info"), py)
        rows.append(("file, buffered", py_ns, per_call_ns(SP_CALL.format("info"), sp)))
        sp.flush_on(spd.Level.INFO)
        rows.append(("file, flush every message", py_ns, per_call_ns(SP_CALL.format("info"), sp)))

        # Async file logging: the caller only formats and enqueues
        sp_async = spd.AsyncLogger(
            "bench.async", spd.BasicFileSink(os.path.join(tmp, "async.log")),
            overflow_policy=spd.OverflowPolicy.BLOCK,
        )
        rows.append((
            "file, async (spdlog only)",
            float("nan"),
            per_call_ns(SP_CALL.format("info"), sp_async),
        ))
        sp_async.flush()
        for handler in logging.getLogger("bench.file").handlers:
            handler.close()

    print(f"{N:,} calls per run, best of 5 (ns per call; lower is better)\n")
    print(f"{'case':<28} {'logging':>10} {'pyspdlog':>10} {'speedup':>8}")
    for case, py_ns, sp_ns in rows:
        speedup = py_ns / sp_ns if py_ns == py_ns else float("nan")
        py_text = f"{py_ns:10.0f}" if py_ns == py_ns else f"{'-':>10}"
        sp_text = f"{sp_ns:10.0f}"
        speed_text = f"{speedup:7.1f}x" if speedup == speedup else f"{'-':>8}"
        print(f"{case:<28} {py_text} {sp_text} {speed_text}")


if __name__ == "__main__":
    main()
