# 16. API reference

Everything below is importable from `pyspdlog`. Arguments typed `Level | str` accept a
`Level` or a level name ([4. Levels](04-levels.md)). Paths accept `str` or any
`os.PathLike`.

- [Constants](#constants) · [Enums](#enums) · [Exceptions](#exceptions)
- [`Logger`](#logger) · [`AsyncLogger`](#asynclogger) · [`LogMessage`](#logmessage)
- [Sinks](#sinks)
- Functions: [logging](#logging-through-the-default-logger) ·
  [registry](#registry) · [global settings](#global-settings) ·
  [factories](#factories) · [async and lifecycle](#async-and-lifecycle) ·
  [levels](#level-helpers)
- [`SpdlogHandler`](#spdloghandler)

## Constants

| Name | Value |
| --- | --- |
| `__version__` | pyspdlog's version, e.g. `"0.1.0"` |
| `SPDLOG_VERSION` | the bundled spdlog's version, e.g. `"1.17.0"` |

## Enums

| Enum | Members |
| --- | --- |
| `Level` (`IntEnum`) | `TRACE`=0, `DEBUG`=1, `INFO`=2, `WARN`=3, `ERROR`=4, `CRITICAL`=5, `OFF`=6 |
| `ColorMode` | `ALWAYS`, `AUTOMATIC`, `NEVER` |
| `OverflowPolicy` | `BLOCK`, `OVERRUN_OLDEST`, `DISCARD_NEW` |
| `PatternTimeType` | `LOCAL`, `UTC` |

## Exceptions

**`SpdlogError(Exception)`**: raised for errors reported by spdlog: a duplicate logger
name, a file that cannot be opened, invalid sink parameters (`max_size=0`,
`n_items=0`, a rotation hour outside 0–23), an invalid thread count.

Other errors use the standard exceptions: `ValueError` (unknown level name, negative
flush interval, `queue_size=0`), `TypeError` (wrong argument types, non-callable
callbacks), `RuntimeError` (lifecycle functions called from a background thread).

## `Logger`

```python
Logger(name: str, sinks: Sequence[Sink] = [])
Logger(name: str, sink: Sink)
```

A named logger writing to its sinks. Not registered until you register it.

| Member | Description |
| --- | --- |
| `name: str` | read-only |
| `level: Level` | minimum level that is logged (default `INFO`); assignable with `Level \| str` |
| `set_level(level: Level \| str)` | same as assigning `level` |
| `should_log(level: Level \| str) -> bool` | whether a message at `level` would be logged |
| `trace / debug / info / warn / warning / error / critical(msg, /, *args, **kwargs)` | log at that level; `msg.format(*args, **kwargs)` when there are arguments |
| `log(level: Level \| str, msg, /, *args, **kwargs)` | log at `level` |
| `exception(msg, /, *args, **kwargs)` | log at `ERROR` with the current exception's traceback |
| `flush()` | flush every sink |
| `flush_on(level: Level \| str)` | flush automatically after messages at or above `level` |
| `flush_level: Level` | read-only; the `flush_on()` level (default `OFF`) |
| `set_pattern(pattern: str, time_type: PatternTimeType = LOCAL)` | set the pattern of every sink |
| `sinks: list[Sink]` | read-only; a copy of the sink list |
| `enable_backtrace(n_messages: int)` | keep the last `n_messages` messages of any level |
| `disable_backtrace()` | stop and clear the backtrace |
| `dump_backtrace()` | log the stored backtrace |
| `should_backtrace() -> bool` | whether backtrace is enabled |
| `clone(name: str) -> Logger` | an unregistered copy with the same sinks and settings (an `AsyncLogger` for async loggers) |

## `AsyncLogger`

```python
AsyncLogger(name: str, sinks: Sequence[Sink] = [], overflow_policy: OverflowPolicy = OverflowPolicy.BLOCK)
AsyncLogger(name: str, sink: Sink, overflow_policy: OverflowPolicy = OverflowPolicy.BLOCK)
```

Subclass of `Logger` whose sinks run on the shared thread pool
([9. Async logging](09-async.md)). Differences from `Logger`:

| Member | Description |
| --- | --- |
| `flush()` | flush the sinks and wait until every message queued so far has been processed (no wait when called from a pool thread) |

## `LogMessage`

Passed to `CallbackSink` callbacks. All attributes are read-only.

| Attribute | Type | Description |
| --- | --- | --- |
| `logger_name` | `str` | name of the logger |
| `level` | `Level` | the message's level |
| `payload` | `str` | the message text |
| `formatted` | `str` | the message with the sink's pattern, without a trailing newline |
| `time` | `datetime.datetime` | time of the call, naive local time |
| `thread_id` | `int` | OS id of the logging thread (`threading.get_native_id()`) |

## Sinks

All sinks derive from `Sink`, which cannot be instantiated.

**`Sink`**

| Member | Description |
| --- | --- |
| `level: Level` | minimum level written (default `TRACE`); assignable with `Level \| str` |
| `set_level(level: Level \| str)` | same as assigning `level` |
| `should_log(level: Level \| str) -> bool` | whether the sink would write a message at `level` |
| `set_pattern(pattern: str)` | set this sink's pattern |
| `flush()` | flush buffered output |

**Console**

| Class | Constructor |
| --- | --- |
| `StdoutColorSink` | `StdoutColorSink(color_mode: ColorMode = AUTOMATIC)` |
| `StderrColorSink` | `StderrColorSink(color_mode: ColorMode = AUTOMATIC)` |
| `StdoutSink` | `StdoutSink()` |
| `StderrSink` | `StderrSink()` |

**Files**

| Class | Constructor | Extra members |
| --- | --- | --- |
| `BasicFileSink` | `BasicFileSink(filename, truncate=False)` | `filename: str`, `truncate()` |
| `RotatingFileSink` | `RotatingFileSink(filename, max_size: int, max_files: int, rotate_on_open=False)` | `filename: str`, `rotate_now()` |
| `DailyFileSink` | `DailyFileSink(filename, rotation_hour=0, rotation_minute=0, truncate=False, max_files=0)` | `filename: str` (the current file) |

**Other**

| Class | Constructor | Extra members |
| --- | --- | --- |
| `RingbufferSink` | `RingbufferSink(n_items: int)` | `last_formatted(limit: int = 0) -> list[str]` |
| `NullSink` | `NullSink()` | |
| `CallbackSink` | `CallbackSink(callback: Callable[[LogMessage], object], flush: Callable[[], object] \| None = None)` | `callback` |
| `StreamSink` | `StreamSink(stream: TextIO \| None = None)` | `stream` (`None` = current `sys.stdout`) |

## Functions

### Logging through the default logger

```python
trace(msg, /, *args, **kwargs)
debug(msg, /, *args, **kwargs)
info(msg, /, *args, **kwargs)
warn(msg, /, *args, **kwargs)
warning(msg, /, *args, **kwargs)
error(msg, /, *args, **kwargs)
critical(msg, /, *args, **kwargs)
log(level: Level | str, msg, /, *args, **kwargs)
exception(msg, /, *args, **kwargs)
```

They behave like the `Logger` methods, and do nothing when there is no default logger.

### Registry

| Function | Description |
| --- | --- |
| `get(name: str) -> Logger \| None` | the registered logger called `name` |
| `register_logger(logger: Logger)` | register; `SpdlogError` if the name is taken |
| `register_or_replace(logger: Logger)` | register, replacing an existing logger with the same name |
| `initialize_logger(logger: Logger)` | apply the global settings, then register (if automatic registration is on) |
| `drop(name: str)` | unregister a logger |
| `drop_all()` | unregister every logger, including the default logger |
| `all_loggers() -> list[Logger]` | registered loggers, sorted by name |
| `default_logger() -> Logger \| None` | the default logger |
| `set_default_logger(logger: Logger \| None)` | set (and register) the default logger; `None` disables module-level logging |
| `set_automatic_registration(enabled: bool)` | whether factories and `initialize_logger()` register loggers |

### Global settings

| Function | Description |
| --- | --- |
| `set_level(level: Level \| str)` | level of every registered logger and of loggers created later |
| `get_level() -> Level` | the default logger's level (`OFF` without one) |
| `should_log(level: Level \| str) -> bool` | whether the default logger would log at `level` |
| `set_pattern(pattern: str, time_type: PatternTimeType = LOCAL)` | pattern of every registered logger and of loggers created later |
| `flush_on(level: Level \| str)` | flush level of every registered logger and of loggers created later |
| `flush_every(interval: float \| timedelta)` | flush registered loggers periodically from a background thread; `0` stops |
| `enable_backtrace(n_messages: int)` | enable backtrace on every registered logger and on loggers created later |
| `disable_backtrace()` | disable it everywhere |
| `dump_backtrace()` | dump the default logger's backtrace |
| `load_env_levels(var: str = "SPDLOG_LEVEL")` | set levels from an environment variable |

### Factories

Create a logger with one sink, apply the global settings and register it. With
`asynchronous=True`, they return an `AsyncLogger` using `OverflowPolicy.BLOCK`.

```python
stdout_color_mt(name, color_mode=ColorMode.AUTOMATIC, *, asynchronous=False) -> Logger
stderr_color_mt(name, color_mode=ColorMode.AUTOMATIC, *, asynchronous=False) -> Logger
stdout_logger_mt(name, *, asynchronous=False) -> Logger
stderr_logger_mt(name, *, asynchronous=False) -> Logger
basic_logger_mt(name, filename, truncate=False, *, asynchronous=False) -> Logger
rotating_logger_mt(name, filename, max_size, max_files, rotate_on_open=False, *, asynchronous=False) -> Logger
daily_logger_mt(name, filename, hour=0, minute=0, truncate=False, max_files=0, *, asynchronous=False) -> Logger
null_logger_mt(name) -> Logger
```

### Async and lifecycle

| Function | Description |
| --- | --- |
| `init_thread_pool(queue_size: int = 8192, thread_count: int = 1)` | replace the shared async thread pool; call before creating async loggers |
| `shutdown()` | flush and drop every logger, stop the background threads; called automatically at exit |

### Level helpers

| Function | Description |
| --- | --- |
| `level_from_str(name: str) -> Level` | parse a level name; `ValueError` if unknown |
| `level_from_logging(levelno: int) -> Level` | map a `logging` level number to a `Level` (rounding down) |

## `SpdlogHandler`

```python
SpdlogHandler(logger: Logger | str | None = None, level: int | str = logging.NOTSET)
```

A `logging.Handler` that sends records to a spdlog logger: a `Logger`, the name of a
registered logger, or `None` for the default logger, looked up for every record. See
[12. The `logging` module](12-logging-module.md).

| Member | Description |
| --- | --- |
| `target: Logger \| None` | the logger records currently go to |
| `emit(record)` | standard `logging.Handler` method |
| `flush()` | flush the target logger |

---

[15. Troubleshooting](15-troubleshooting.md) · [Index](README.md) · Next: [17. Design notes](17-design-notes.md)
