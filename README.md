# PySpdlog

[![CI](https://github.com/Wiill007/spdlog-python-bindings/actions/workflows/ci.yml/badge.svg)](https://github.com/Wiill007/spdlog-python-bindings/actions/workflows/ci.yml)

Python bindings for the [spdlog](https://github.com/gabime/spdlog) C++ logging
library, built with [nanobind](https://github.com/wjakob/nanobind).

```python
import pyspdlog as spd

log = spd.stdout_color_mt("app")
log.info("listening on {}:{}", "0.0.0.0", 8080)
log.debug("only formatted if DEBUG is enabled: {}", expensive_value)
```

- **Fast.** Formatting, filtering and I/O happen in C++; writing to a file is 8-16x
  faster than the `logging` module (see [Performance](#performance)).
- **Pythonic.** `str.format()` messages formatted only when the level is enabled,
  levels by name (`"debug"`), `logger.exception()`, and a `logging.Handler` bridge.
- **Everything spdlog does.** Console, file, rotating, daily and in-memory sinks,
  patterns, per-sink levels, backtrace, async loggers with overflow policies,
  periodic flushing, `SPDLOG_LEVEL` environment configuration.
- **Python sinks.** Send records to any callable or text stream, also from async
  worker threads.
- **Safe with threads.** The GIL is released while spdlog works; background threads
  are stopped cleanly at exit and handled across `os.fork()`.
- **Typed.** Ships a `.pyi` stub and `py.typed`.

Requires Python 3.10+. Python 3.12+ uses a single abi3 wheel. spdlog 1.17 is compiled
into the extension, so there is nothing else to install.

## Installation

pyspdlog is not on PyPI yet. Install it from GitHub. That builds it from source, so
it needs a C++17 compiler (CMake is downloaded automatically if it is missing):

```sh
pip install git+https://github.com/Wiill007/spdlog-python-bindings
```

## Usage

### Loggers and sinks

A logger has a name and one or more sinks. The `*_mt()` factories create a logger
with one sink and register it, so it can be retrieved with `spd.get(name)`:

```python
log = spd.basic_logger_mt("app", "logs/app.log")
assert spd.get("app") is log
```

To combine sinks, create a `Logger` yourself. Each sink can have its own level
and pattern:

```python
console = spd.StdoutColorSink()
console.set_level("warning")

file = spd.RotatingFileSink("logs/app.log", max_size=10 * 1024 * 1024, max_files=5)
file.set_pattern("%Y-%m-%d %H:%M:%S.%e [%l] [%t] %v")

log = spd.Logger("app", [console, file])
spd.register_logger(log)  # optional: make it available through spd.get("app")
```

| Sink | Writes to |
| --- | --- |
| `StdoutColorSink`, `StderrColorSink` | the console, with colors (`ColorMode.AUTOMATIC`, `ALWAYS`, `NEVER`) |
| `StdoutSink`, `StderrSink` | the console, without colors |
| `BasicFileSink(filename, truncate=False)` | a file |
| `RotatingFileSink(filename, max_size, max_files)` | a file rotated by size: `log.txt` → `log.1.txt` → ... |
| `DailyFileSink(filename, rotation_hour=0, rotation_minute=0)` | a new file every day: `log_2026-01-31.txt` |
| `RingbufferSink(n_items)` | memory; `last_formatted()` returns the newest messages |
| `NullSink` | nowhere |
| `CallbackSink(callback, flush=None)` | a Python callable, called with a `LogMessage` |
| `StreamSink(stream=None)` | a Python text stream (`None` means the current `sys.stdout`) |

File sinks buffer their output. It is written when the logger is flushed: on
`flush()`, at exit, or automatically with `flush_on()` / `spd.flush_every()`.

The console sinks write to the process's file descriptors, so their output does
not appear in Jupyter notebooks. Use `StreamSink()` there.

### Messages

Messages use `str.format()` syntax. Formatting only happens if the message will
actually be logged:

```python
log.info("user {} logged in from {ip}", user.name, ip=request.ip)
log.log("warning", "disk {:.0%} full", 0.93)
```

A message without arguments is used as-is, so `log.info("{}")` logs `{}`. Anything
that is not a `str` is converted with `str()`.

`exception()` logs at ERROR and appends the traceback of the exception being
handled:

```python
try:
    connect()
except OSError:
    log.exception("connection to {} failed", host)
```

### Levels

`Level` is an `IntEnum`: `TRACE`, `DEBUG`, `INFO`, `WARN`, `ERROR`, `CRITICAL`,
`OFF`. Anywhere a level is expected you can also pass a case-insensitive name such as
`"debug"`, `"warning"` or `"err"`.

```python
log.set_level("debug")          # logger: minimum level that is logged (default INFO)
file.set_level(spd.Level.INFO)  # sink: minimum level it writes (default TRACE)
log.flush_on("error")           # flush immediately on ERROR and above
log.should_log("trace")         # False
```

Levels can come from the environment, using spdlog's syntax:

```python
# SPDLOG_LEVEL="info,app=debug,db=off"
spd.load_env_levels()
```

### Patterns

```python
log.set_pattern("[%H:%M:%S.%e] [%n] [%^%l%$] %v")
spd.set_pattern("%+")  # every registered logger, and loggers created later
```

The `%^...%$` markers color part of the line in color sinks. See
[spdlog's documentation](https://github.com/gabime/spdlog/wiki/Custom-formatting)
for every flag. `set_pattern(..., spd.PatternTimeType.UTC)` uses UTC times.

### The registry and the default logger

The module-level functions log through the default logger, which writes colored
output to stdout:

```python
spd.info("hello")
spd.set_default_logger(spd.basic_logger_mt("main", "main.log"))
spd.warning("now in main.log")
```

The registry also applies global settings to every registered logger:
`spd.set_level()`, `spd.set_pattern()`, `spd.flush_on()`, `spd.flush_every()` (a
periodic flush from a background thread) and `spd.enable_backtrace()`. Use
`spd.get(name)`, `spd.drop(name)`, `spd.drop_all()` and `spd.all_loggers()` to manage
registered loggers.

### Backtrace

With backtrace enabled, a logger keeps its last N messages, *including the ones below
its level*, and logs them on demand. That gives you debug context without logging at
DEBUG all the time:

```python
log.enable_backtrace(32)
log.debug("step {}", 1)  # not logged, but remembered
...
log.dump_backtrace()     # logs the remembered messages
```

### Python sinks

`CallbackSink` calls a function for each message:

```python
def send(msg: spd.LogMessage) -> None:
    # msg.logger_name, msg.level, msg.payload, msg.formatted, msg.time, msg.thread_id
    queue.put(msg.formatted)

log = spd.Logger("app", [spd.StdoutColorSink(), spd.CallbackSink(send)])
```

`StreamSink` writes lines to a text stream such as `sys.stderr`, an open file or an
`io.StringIO`.

Callbacks can log, even to the same logger. An exception raised by a callback does
not propagate to the code that logged. It is reported through `sys.unraisablehook`,
like an exception in `__del__`. With an async logger, the callback runs on the
thread pool.

### Async logging

An `AsyncLogger` formats the message on the calling thread and hands it to a
background thread pool, which runs the sinks:

```python
spd.init_thread_pool(queue_size=8192, thread_count=1)  # optional, before creating async loggers

log = spd.AsyncLogger("app", spd.BasicFileSink("app.log"),
                      overflow_policy=spd.OverflowPolicy.BLOCK)
log.info("queued")
log.flush()  # waits until everything queued so far is written
```

The factories take `asynchronous=True`, for example
`spd.basic_logger_mt("app", "app.log", asynchronous=True)`.

When the queue is full, `OverflowPolicy.BLOCK` (the default) waits,
`OVERRUN_OLDEST` drops the oldest queued message and `DISCARD_NEW` drops the new one.

Async logging pays off when sinks are slow or can stall: network or Python sinks,
or files on slow disks. For plain files, a synchronous logger is usually faster,
because spdlog already buffers file writes.

### The `logging` module

`SpdlogHandler` sends `logging` records to a spdlog logger. That way libraries
that use `logging` end up in the same place as your spdlog output:

```python
import logging

logging.basicConfig(
    level=logging.INFO,
    format="%(name)s: %(message)s",  # spdlog's pattern adds the time and level
    handlers=[spd.SpdlogHandler()],  # to the default logger
)
logging.getLogger("urllib3").warning("retrying")
# [2026-01-31 12:00:00.000] [warning] urllib3: retrying
```

`SpdlogHandler(logger)` takes a `Logger`, the name of a registered logger, or `None`
for the default logger. The handler's `Formatter` produces the message
(`%(message)s` by default, plus the traceback). The spdlog pattern then adds the
time, level and so on. `basicConfig()` gives its handlers a formatter that already
includes the level, which is why the example above passes `format=`.

## Threads, exit and fork

- spdlog's work (filtering, formatting the pattern, I/O, waiting for locks) runs
  with the GIL released, so other Python threads keep running.
- At interpreter exit, pyspdlog calls `shutdown()`. That flushes every logger,
  writes the messages still queued for async loggers and stops the background
  threads. Afterwards, async loggers drop new messages quietly.
- After `os.fork()`, the child process has none of the parent's background threads.
  pyspdlog abandons the inherited thread pool and periodic flusher in the child.
  Async loggers created before the fork drop their messages there, with a one-time
  `RuntimeWarning`; async loggers created in the child work normally. For
  multiprocessing, prefer the `spawn` or `forkserver` start methods.
- `shutdown()`, `init_thread_pool()` and `flush_every()` raise `RuntimeError` when
  called from a sink running on a background thread, because they would have to
  wait for that same thread.

## Performance

`benchmarks/bench.py` compares pyspdlog with the `logging` module. Both use a
similar pattern (time, logger name, level, message) and log one message with two
arguments per call. On an Intel Xeon E5-1620 v3 with Python 3.13:

| Case | `logging` | pyspdlog | Speedup |
| --- | ---: | ---: | ---: |
| Level disabled (`debug()` on an INFO logger) | 127 ns | 145 ns | 0.9x |
| Format the record and discard it | 4796 ns | 653 ns | 7.3x |
| File, buffered (spdlog's default) | 12645 ns | 769 ns | 16.4x |
| File, flushed after every message (`logging`'s default) | 12645 ns | 1490 ns | 8.5x |

A disabled call costs about the same as in `logging`: it is almost all Python call
overhead. Run `uv run python benchmarks/bench.py` to measure on your machine.

## Development

You need [uv](https://docs.astral.sh/uv/), CMake 3.15+ and a C++17 compiler. The
first build downloads spdlog.

```sh
uv sync                 # build the extension and install it into .venv
uv run pytest           # run the tests
uv run python benchmarks/bench.py
```

`uv sync` and `uv run` rebuild the extension when a C++ or build file changes.

CMake options (pass them with `uv sync -C cmake.define.NAME=VALUE`, or `-DNAME=VALUE`
when using CMake directly):

| Option | Default | Effect |
| --- | --- | --- |
| `PYSPDLOG_USE_SYSTEM_SPDLOG` | `OFF` | Link the installed spdlog instead of downloading it. The extension then depends on `libspdlog`. |
| `PYSPDLOG_STUBS` | `ON` | Generate `_core.pyi`. This imports the built module, so turn it off when cross-compiling. |

Set `PYSPDLOG_LEAK_WARNINGS=1` to make nanobind report objects still alive at exit.
The exit-time tests enable it.

Wheels are built with [cibuildwheel](https://cibuildwheel.pypa.io/) (configured in
`pyproject.toml`). CI builds them for Linux (x86_64 and aarch64, glibc and musl),
macOS (arm64) and Windows on every push to `main`.

## License

MIT. spdlog and fmt (compiled into the extension) are also MIT-licensed.
