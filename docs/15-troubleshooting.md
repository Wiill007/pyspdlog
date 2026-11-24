# 15. Troubleshooting

## Output

### The log file is empty, or the last lines are missing

File sinks buffer their output ([10. Flushing](10-flushing.md)). Data reaches the file
when the logger is flushed: on `log.flush()`, at normal exit, with `flush_on()`, or
periodically with `flush_every()`. If the process ended with `os._exit()`, a signal or
a crash, or it is a `multiprocessing` child, flush explicitly.

### Nothing appears in Jupyter, `capsys` or `redirect_stdout()`

Console sinks write to file descriptors 1 and 2 directly, bypassing `sys.stdout`. Use
`spd.StreamSink()`, which writes to whatever `sys.stdout` currently is. Under pytest,
`capfd` does capture the console sinks.

### `spd.info()` prints nothing

There is no default logger. `drop_all()`, `drop()` of the default logger's name,
`shutdown()` and `set_default_logger(None)` all remove it, and module-level functions
silently do nothing without one. Set a new one with `spd.set_default_logger(...)`.

### `logging` output shows the level twice: `[warning] WARNING:urllib3:retrying`

`logging.basicConfig()` gave the handler a formatter with
`%(levelname)s:%(name)s:%(message)s`. Pass `format="%(name)s: %(message)s"` (or
`"%(message)s"`). See [12](12-logging-module.md).

### `%s`, `%#`, `%!` or `%@` in the pattern are empty

pyspdlog does not pass Python source locations to spdlog. With `SpdlogHandler`, put
`%(filename)s:%(lineno)d` in the `logging.Formatter` instead.

### Patterns I set on a sink were replaced

`log.set_pattern()`, `spd.set_pattern()`, `spd.initialize_logger()` and the factories set
the pattern of every sink involved. Set per-sink patterns after those calls.

### `RingbufferSink.last_formatted()` shows odd times or elapsed values

The ring buffer formats stored messages when you call `last_formatted()`, with the
sink's pattern at that moment. Set the pattern before logging and leave it, and do not
rely on elapsed-time flags there.

### Colors are missing (or there are escape codes in a file)

`ColorMode.AUTOMATIC` only colors when writing to a terminal whose `TERM` /
`COLORTERM` say it supports colors. Use `ColorMode.ALWAYS` to force colors, or
`ColorMode.NEVER` (or `StdoutSink`) to get plain text. Only the text between `%^` and
`%$` in the pattern is colored.

## Errors

### `SpdlogError: logger with name 'x' already exists`

The factories register the loggers they create, and names are unique. Reuse the
existing logger with `spd.get("x")`, choose another name, `spd.drop("x")` first, or
register with `spd.register_or_replace()`.

### `IndexError` / `KeyError` / `ValueError` from a logging call

The message has `{}` placeholders that do not match the arguments. With arguments,
literal braces must be doubled: `log.info("{{literal}} {}", value)`. Without arguments,
the message is used as-is ([3. Messages](03-messages.md)).

### `ValueError: unknown log level 'x'`

Level names are `trace`, `debug`, `info`, `warn`/`warning`, `err`/`error`, `critical`
and `off`, in any case.

### `Exception ignored in: 'pyspdlog.CallbackSink callback'`

Your callback raised. The exception was reported through `sys.unraisablehook` instead
of propagating ([8. Python sinks](08-python-sinks.md)). `'pyspdlog.StreamSink write'`
is the same thing for a stream's `write()` or `flush()`.

### `RuntimeError: shutdown() cannot be called from a sink running on a background thread`

A callback running on the thread pool or the periodic flusher called `shutdown()`,
`init_thread_pool()` or `flush_every()`. Call them from your own threads.

### `[*** LOG ERROR #0001 ***] ... async log: thread pool doesn't exist anymore`

`init_thread_pool()` was called after creating that async logger, which replaced the
pool it was using. Call `init_thread_pool()` first, then create async loggers.

### `RuntimeWarning: pyspdlog: AsyncLogger 'x' was created before os.fork() ...`

The process forked and the child is using an async logger from the parent. Its
messages are dropped. Create async loggers in the child, or use the `spawn` /
`forkserver` start methods ([13](13-threads-exit-fork.md#fork)).

### `SpdlogError: Failed opening file ... for writing`

The path is a directory, or you lack permissions. Missing parent directories are
created automatically.

## Async loggers

### Messages are missing

- The process ended without `atexit` (see the first entry above) before the queue was
  processed. Call `log.flush()`.
- The policy is `OVERRUN_OLDEST` or `DISCARD_NEW` and the queue filled up. Use `BLOCK`,
  or a larger `init_thread_pool(queue_size=...)`.
- `shutdown()` was called, or the process forked: async loggers created before that drop
  messages.

### Messages are out of order

The thread pool has more than one worker (`init_thread_pool(thread_count > 1)`), so
messages are processed in parallel. Use one worker when order matters.

### `msg.thread_id` is not the worker thread / does not match `threading.get_ident()`

`LogMessage.thread_id` is the OS thread id of the thread that *logged*, as
`threading.get_native_id()` returns it. `threading.get_ident()` is a different number.

## Performance

### Disabled `debug()` calls still take time

Python evaluates the arguments before the call. Guard expensive ones with
`if log.should_log("debug"):`. Also check that backtrace is not enabled on that logger
([11](11-backtrace.md)).

## Exit

### `nanobind: leaked N instances!` at exit

This report only appears with `PYSPDLOG_LEAK_WARNINGS=1`. It is expected for objects
used by daemon threads, and in children of `fork()`. Anything else is worth a bug
report.

## Platforms

### It does not build or run on Windows

Windows is not supported.

---

[14. Performance](14-performance.md) · [Index](README.md) · Next: [16. API reference](16-api-reference.md)
