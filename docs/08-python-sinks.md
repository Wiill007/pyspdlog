# 8. Python sinks

Two sinks hand messages to Python code: `CallbackSink` calls a function, and
`StreamSink` writes to a text stream. They behave like the other sinks: they have a
level and a pattern, they can be shared, and they work with async loggers.

## `CallbackSink`

```python
spd.CallbackSink(callback, flush=None)
```

`callback` is called with one `LogMessage` for each message the sink accepts.
`flush`, if given, is called without arguments when the sink is flushed.

```python
import queue

outbox: queue.Queue[str] = queue.Queue()

def to_outbox(msg: spd.LogMessage) -> None:
    if msg.level >= spd.Level.ERROR:
        outbox.put(msg.formatted)

alerts = spd.CallbackSink(to_outbox)
alerts.set_pattern("%Y-%m-%d %H:%M:%S %n: %v")
log = spd.Logger("app", [spd.StdoutColorSink(), alerts])
```

Both arguments must be callable, otherwise the constructor raises `TypeError`.
`sink.callback` returns the callback.

### `LogMessage`

| Attribute | Type | Content |
| --- | --- | --- |
| `logger_name` | `str` | name of the logger that logged it |
| `level` | `Level` | its level |
| `payload` | `str` | the message text, without the pattern |
| `formatted` | `str` | the message rendered with **the sink's** pattern, without a trailing newline |
| `time` | `datetime.datetime` | when it was logged, as a naive local time |
| `thread_id` | `int` | OS id of the thread that *logged* it (compare with `threading.get_native_id()`) |

`thread_id` is the thread that called `log.info()`. With an async logger, that is
not the worker thread that runs the callback.

## `StreamSink`

```python
spd.StreamSink(stream=None)
```

`StreamSink` calls `stream.write(line + "\n")` for each message and `stream.flush()`
when flushed. Any object with `write()` and `flush()` works: `sys.stderr`, a file
opened in text mode, `io.StringIO`, and so on.

With `stream=None` (the default), it writes to **whatever `sys.stdout` is at that
moment**. That makes output show up where Python's own output does:

- in Jupyter notebook cells,
- under pytest's `capsys`,
- inside `contextlib.redirect_stdout()`.

```python
log = spd.Logger("nb", spd.StreamSink())
log.info("visible in the notebook")
```

`sink.stream` returns the stream (`None` for the current `sys.stdout`).

## Errors in Python sinks

An exception in a callback, or in a stream's `write()`, never propagates to the code
that logged. The logging call may be on another thread, so there is nobody to raise
to. The exception goes to
[`sys.unraisablehook`](https://docs.python.org/3/library/sys.html#sys.unraisablehook),
like an exception in `__del__`. By default it is printed to stderr:

```text
Exception ignored in: 'pyspdlog.CallbackSink callback'
Traceback (most recent call last):
  File "app.py", line 3, in to_outbox
ValueError: boom
```

Under pytest it becomes a `PytestUnraisableExceptionWarning`.

## Threads and reentrancy

- With a regular `Logger`, the callback runs on the thread that logged.
- With an `AsyncLogger`, it runs on a thread pool worker. With
  `init_thread_pool(thread_count > 1)`, callbacks can run on several workers at
  once, so make them thread-safe.
- Callbacks run with the GIL held, like any Python code. Keep them short. For slow
  work, put the message in a `queue.Queue` and process it elsewhere.
- A callback may log, even to the logger that called it. pyspdlog never holds a lock
  while running Python code, so this cannot deadlock. It can recurse, though: a
  callback that always logs to its own logger ends in `RecursionError`, reported
  through `sys.unraisablehook`.

## Lifetime

A sink keeps its callback (or stream) alive. Two consequences:

- **Reference cycles.** A callback that refers to its own logger, directly or
  through module globals, forms a cycle through C++ that Python's garbage
  collector cannot see. Long-lived loggers are not affected, because they live
  until exit anyway. At exit, pyspdlog breaks these cycles by releasing every
  callback and stream after shutting down. For loggers you create and discard
  repeatedly, avoid referring to the logger from its callback, or use
  `weakref.ref`.
- **After exit cleanup**, Python sinks do nothing. That only matters for code that
  runs after pyspdlog's `atexit` handler: `atexit` handlers registered *before*
  pyspdlog was imported, and `__del__` methods during interpreter shutdown.

---

[7. The registry](07-registry.md) · [Index](README.md) · Next: [9. Async logging](09-async.md)
