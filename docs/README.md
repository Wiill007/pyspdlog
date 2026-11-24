# pyspdlog documentation

pyspdlog brings [spdlog](https://github.com/gabime/spdlog), a fast C++ logging library,
to Python. You log from Python; filtering, pattern formatting and I/O happen in C++.

```python
import pyspdlog as spd

log = spd.stdout_color_mt("app")
log.info("hello {}", "world")
```

The pages are meant to be read in order. Each one ends with a link to the next.

## Using pyspdlog

1. [Installation](01-installation.md): requirements, installing, building options.
2. [Quick start](02-quickstart.md): the ten-minute tour.
3. [Messages](03-messages.md): `str.format()` messages, lazy formatting, `exception()`.
4. [Levels](04-levels.md): `Level`, level names, logger and sink levels, `SPDLOG_LEVEL`.
5. [Sinks](05-sinks.md): console, files, rotation, memory, and choosing between them.
6. [Patterns](06-patterns.md): the output format, every pattern flag, colors.
7. [The registry and the default logger](07-registry.md): names, factories, global settings.
8. [Python sinks](08-python-sinks.md): `CallbackSink`, `StreamSink` and `LogMessage`.
9. [Async logging](09-async.md): `AsyncLogger`, the thread pool, overflow policies.
10. [Flushing](10-flushing.md): when messages actually reach the file.
11. [Backtrace](11-backtrace.md): keep debug context without logging it.
12. [The `logging` module](12-logging-module.md): `SpdlogHandler` and going the other way.

## Going deeper

13. [Threads, exit and fork](13-threads-exit-fork.md): the GIL, interpreter exit,
    `os.fork()`, daemon threads and reference cycles.
14. [Performance](14-performance.md): benchmark numbers and how to stay fast.
15. [Troubleshooting](15-troubleshooting.md): symptoms, causes and fixes.
16. [API reference](16-api-reference.md): every class, method and function.
17. [Design notes](17-design-notes.md): how it works inside, the problems found while
    building it, and the known limitations.
18. [Development](18-development.md): building, testing, benchmarking, releasing.

## Platform support

| | Supported |
| --- | --- |
| Linux (x86_64, aarch64; glibc and musl) | yes (tested in CI) |
| macOS (arm64, x86_64) | expected to work, from source; not tested in CI |
| Windows | no |
| CPython 3.10, 3.11 | yes (one wheel per version) |
| CPython 3.12 and newer | yes (a single abi3 wheel) |
| Free-threaded CPython (3.13t, 3.14t) | no |
| PyPy | no |

The bundled spdlog is 1.17.0, available as `pyspdlog.SPDLOG_VERSION`.
