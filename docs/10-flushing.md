# 10. Flushing

*Flushing* moves buffered output to its destination. Until a file sink is flushed,
recent messages can be in memory only. They are lost if the process dies without
cleaning up.

## Who buffers

| Sink | Buffering |
| --- | --- |
| `BasicFileSink`, `RotatingFileSink`, `DailyFileSink` | buffered (C `stdio` buffer); written when full or flushed |
| `StdoutSink`, `StderrSink`, color sinks | flushed after every message |
| `StreamSink` | each message is passed to `stream.write()`; the stream decides |
| `CallbackSink` | each message goes straight to the callback |
| `RingbufferSink`, `NullSink` | nothing to flush |
| any sink of an `AsyncLogger` | also waits in the queue before reaching the sink |

## Ways to flush

```python
log.flush()                    # now (for an AsyncLogger, also waits for the queue)
log.flush_on("error")          # automatically after every ERROR or CRITICAL message
spd.flush_on("warning")        # the same, for every registered logger
spd.flush_every(1.0)           # every second, from a background thread
spd.flush_every(datetime.timedelta(milliseconds=500))
spd.flush_every(0)             # stop the periodic flush
sink.flush()                   # one sink
```

- `log.flush_level` returns the level set with `flush_on()` (`OFF` by default, which
  means never).
- `flush_every()` only flushes **registered** loggers, and replaces any previous
  interval. It takes seconds as a number, or a `timedelta`. Negative values raise
  `ValueError`.

At normal interpreter exit, pyspdlog flushes every logger (see
[13](13-threads-exit-fork.md)). To flush loggers that are not registered, keep a
reference and call `flush()` yourself, or register them.

## Choosing a strategy

| Situation | Suggestion |
| --- | --- |
| Interactive tools, scripts | nothing: exit flushes |
| Services that may be killed | `flush_on("warning")` or `"error"`, plus `flush_every(1)` |
| You need every line on disk immediately | `flush_on("trace")`. It costs about 2x per message compared to buffered writes |
| `multiprocessing` workers, `os._exit()` | call `flush()` before the process ends; `atexit` does not run there |

Flushing writes to the operating system, but it does not force the data onto the
physical disk (no `fsync`). That protects against your process crashing, not
against a power cut.

---

[9. Async logging](09-async.md) · [Index](README.md) · Next: [11. Backtrace](11-backtrace.md)
