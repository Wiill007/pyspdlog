# 7. The registry and the default logger

## The registry

The registry is a process-wide table of loggers by name. Registering a logger lets
any part of the program find it, and lets global settings reach it.

```python
log = spd.Logger("db", spd.StdoutColorSink())
spd.register_logger(log)

spd.get("db") is log      # True: the same Python object
spd.get("nope")           # None
```

| Function | Does |
| --- | --- |
| `spd.register_logger(log)` | register `log` under its name; `SpdlogError` if the name is taken |
| `spd.register_or_replace(log)` | register `log`, replacing a logger with the same name |
| `spd.initialize_logger(log)` | apply the global settings to `log`, then register it (see below) |
| `spd.get(name)` | the registered logger, or `None` |
| `spd.all_loggers()` | every registered logger, sorted by name |
| `spd.drop(name)` | unregister one logger (no error if it does not exist) |
| `spd.drop_all()` | unregister every logger, **including the default logger** |

Dropping only removes the registry's reference. A logger you still hold keeps
working.

Creating a `Logger(...)` or `AsyncLogger(...)` directly does **not** register it.

## Factories

The factories create a logger with one sink, apply the global settings and register
it, in one call. Their names are spdlog's (`_mt` stands for "multi-threaded"):

| Factory | Sink |
| --- | --- |
| `stdout_color_mt(name, color_mode=AUTOMATIC)` | `StdoutColorSink` |
| `stderr_color_mt(name, color_mode=AUTOMATIC)` | `StderrColorSink` |
| `stdout_logger_mt(name)` | `StdoutSink` |
| `stderr_logger_mt(name)` | `StderrSink` |
| `basic_logger_mt(name, filename, truncate=False)` | `BasicFileSink` |
| `rotating_logger_mt(name, filename, max_size, max_files, rotate_on_open=False)` | `RotatingFileSink` |
| `daily_logger_mt(name, filename, hour=0, minute=0, truncate=False, max_files=0)` | `DailyFileSink` |
| `null_logger_mt(name)` | `NullSink` |

All but `null_logger_mt` take a keyword-only `asynchronous=True` to create an
[`AsyncLogger`](09-async.md) instead:

```python
log = spd.rotating_logger_mt("app", "logs/app.log", 10_000_000, 5, asynchronous=True)
```

Calling a factory twice with the same name raises
`SpdlogError: logger with name 'app' already exists`. Use `spd.get()` to reuse the
existing logger.

### Automatic registration

`spd.set_automatic_registration(False)` makes the factories and `initialize_logger()`
stop registering loggers. They still apply the global settings.

## Global settings

These functions change every registered logger now, and are remembered for loggers
created later by the factories and `initialize_logger()`:

| Function | Sets |
| --- | --- |
| `spd.set_level(level)` | the logger level |
| `spd.set_pattern(pattern, time_type=LOCAL)` | the pattern (replacing per-sink patterns) |
| `spd.flush_on(level)` | the automatic flush level ([10](10-flushing.md)) |
| `spd.enable_backtrace(n)` / `spd.disable_backtrace()` | backtrace ([11](11-backtrace.md)) |
| `spd.load_env_levels(var="SPDLOG_LEVEL")` | levels from the environment ([4](04-levels.md)) |

`spd.flush_every(interval)` is also global: it periodically flushes every registered
logger ([10](10-flushing.md)).

Loggers you construct yourself start with spdlog's defaults (level `INFO`, pattern
`%+`). Pass them to `spd.initialize_logger(log)` to give them the global settings.

## The default logger

The module-level functions `spd.trace()` … `spd.critical()`, `spd.log()`,
`spd.exception()`, `spd.get_level()`, `spd.should_log()` and `spd.dump_backtrace()`
all use the *default logger*. When pyspdlog is imported, the default logger is a
logger with an empty name and a `StdoutColorSink`.

```python
spd.default_logger()                       # <Logger '' level=info>
spd.set_default_logger(spd.basic_logger_mt("main", "main.log"))
spd.info("goes to main.log")
```

- `set_default_logger(log)` also registers `log`.
- `set_default_logger(None)` turns module-level logging off. The previous default
  logger stays registered under its name.
- `drop_all()`, `drop(<the default logger's name>)` and `shutdown()` remove the
  default logger.

**Without a default logger, the module-level functions do nothing.** They do not
raise, and `default_logger()` returns `None`.

## `shutdown()`

`spd.shutdown()` flushes and drops every logger, stops the periodic flusher and the
async thread pool (after processing the queued messages). pyspdlog calls it
automatically when the interpreter exits, so you rarely need it. See
[13. Threads, exit and fork](13-threads-exit-fork.md).

---

[6. Patterns](06-patterns.md) · [Index](README.md) · Next: [8. Python sinks](08-python-sinks.md)
