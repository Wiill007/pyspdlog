# 14. Performance

## Numbers

`benchmarks/bench.py` compares pyspdlog with the `logging` module. Both use a similar
pattern (time, logger name, level, message), and every call logs one message with two
arguments (`"value {} {}", 1, "x"`, and `"value %s %s"` for `logging`).

Intel Xeon E5-1620 v3 @ 3.50GHz, Python 3.13, Linux; nanoseconds per call (lower is
better):

| Case | `logging` | pyspdlog | Speedup |
| --- | ---: | ---: | ---: |
| Level disabled (`debug()` on an INFO logger) | 127 | 145 | 0.9x |
| Format the record and discard it | 4796 | 653 | 7.3x |
| File, buffered (spdlog's default) | 12645 | 769 | 16.4x |
| File, flushed after every message (`logging`'s default) | 12645 | 1490 | 8.5x |
| File through an `AsyncLogger` (time spent by the caller) | – | 1344 | – |

Run it on your machine:

```sh
uv run python benchmarks/bench.py
```

## What the numbers mean

- **Disabled calls cost the same as in `logging`.** About 130 ns, nearly all of it the
  cost of calling a function from Python and passing it `*args`. The level check
  itself is a few nanoseconds.
- **Enabled calls are much cheaper.** `logging` creates a `LogRecord` object, calls
  several Python methods, and formats the time with `time.strftime()`. pyspdlog does
  one `str.format()` and hands a string to C++, where the pattern and the I/O are fast.
- **Flushing every message costs about 2x** compared to buffered writes. `logging`'s
  `FileHandler` always flushes; spdlog only flushes when told to.
- **Async is not a throughput win for local files.** Queueing a message (a lock, a
  copy, waking the worker) costs more than a buffered `fwrite()`. Use async for slow or
  unpredictable sinks ([9. Async logging](09-async.md)).

## Staying fast

- **Pass arguments instead of using f-strings.** `log.debug("x={}", x)` skips the
  formatting when DEBUG is off; `log.debug(f"x={x}")` always formats.
- **Guard expensive arguments.** Arguments are evaluated before the call:
  `if log.should_log("debug"): log.debug("{}", expensive())`.
- **Keep the level high in hot loops.** Every enabled message is formatted at least
  once, even when every sink filters it out by level. Raise the logger's level rather
  than only the sinks' levels.
- **Avoid backtrace on hot paths.** With backtrace on, every message is formatted,
  including disabled ones ([11. Backtrace](11-backtrace.md)).
- **Python sinks cost one Python call per message**, plus creating a `LogMessage` for
  `CallbackSink`. Prefer the C++ sinks for high-volume output, and use Python sinks
  where you need Python.
- **Choose `flush_on()` levels deliberately.** `flush_on("error")` costs nothing in
  normal operation; `flush_on("trace")` halves file throughput.
- **Prefer `StdoutSink`/`StdoutColorSink` over `StreamSink(sys.stdout)`** outside of
  Jupyter. The console sinks write from C++ with the GIL released.

---

[13. Threads, exit and fork](13-threads-exit-fork.md) · [Index](README.md) · Next: [15. Troubleshooting](15-troubleshooting.md)
