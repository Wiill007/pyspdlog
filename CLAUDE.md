# pyspdlog

nanobind bindings for spdlog 1.17 (fetched by CMake), packaged with scikit-build-core.

## Commands

```sh
uv sync                                   # build + install into .venv (rebuilds when C++ changes)
uv run pytest                             # full suite, ~2 s
uv run pytest tests/test_async.py -k fork # one area
uv run python benchmarks/bench.py         # compare with the logging module
uv build                                  # wheel + sdist
```

For faster C++ iteration, build with CMake directly and import `_core` from the
build dir (`PYTHONPATH=<build dir> python -c "import _core"`):
`cmake -S . -B /tmp/b -G Ninja -DPython_EXECUTABLE=.venv/bin/python && cmake --build /tmp/b`.

## Layout

- `src/cpp/common.h`: shared declarations, the `LevelArg` type caster (accepts `Level` or a name),
  and the threading rules below.
- `src/cpp/module.cpp`: `NB_MODULE`, enums, `SpdlogError`.
- `src/cpp/logger.cpp`: `Logger` / `AsyncLogger` and `log_message()` (formatting + GIL release).
- `src/cpp/sinks.cpp`: sinks, including the Python-backed `CallbackSink` / `StreamSink`.
- `src/cpp/registry.cpp`: registry functions, `*_mt()` factories, module-level log functions.
- `src/cpp/threads.cpp`: async thread pool, periodic flusher, `shutdown()`, fork handlers.
- `src/pyspdlog/`: the Python package (re-exports `_core`, `atexit` / `register_at_fork` hooks,
  `SpdlogHandler`). The `.pyi` stub is generated at build time by nanobind stubgen.

## Threading rules (do not break these)

1. Never wait on a spdlog mutex while holding the GIL: spdlog runs sinks under its locks and
   Python-backed sinks need the GIL. Bindings that may lock release the GIL first
   (`release_gil()` call guard). Arguments are converted before the guard, return values after.
2. Python-backed sinks (`python_sink` in sinks.cpp) never hold their own mutex while calling Python.
3. Code that touches Python from spdlog threads uses `nb::detail::cleanup_guard`.
4. Never join a background thread from itself: `shutdown()`, `init_thread_pool()`, `flush_every()`
   check `t_on_pool_thread` / `t_on_flusher_thread`.
5. Globals used by background threads are heap-allocated and never freed (static destruction order).

## Gotchas

- `spdlog::async_logger` is `final` and its pool is private, so the pool of each async logger is
  tracked in a side table (threads.cpp) to detect loggers whose pool died (shutdown / fork).
- spdlog's async `flush()` does not wait; `flush_async_logger()` adds a barrier on the pool.
- `spdlog::get_level()`, `should_log()` and `dump_backtrace()` dereference the default logger
  without a null check. The bindings reimplement them.
- Positional-only parameters in nanobind are unnamed `nb::arg()` annotations; the log methods use
  them (plus `nb::sig`) so `info("{level}", level=1)` works.
- Python references held in C++ (sinks inside loggers) are invisible to the GC. Cycles are broken at
  exit by `_release_python_sinks()`.
- Exit-time behavior can only be tested in a subprocess: use `run_python()` from
  `tests/conftest.py`, which enables `PYSPDLOG_LEAK_WARNINGS`.
