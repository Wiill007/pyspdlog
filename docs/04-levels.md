# 4. Levels

## The `Level` enum

`Level` is an `enum.IntEnum`, so its members compare like integers:

| Member | Value | `%l` in patterns | `%L` |
| --- | ---: | --- | --- |
| `Level.TRACE` | 0 | `trace` | `T` |
| `Level.DEBUG` | 1 | `debug` | `D` |
| `Level.INFO` | 2 | `info` | `I` |
| `Level.WARN` | 3 | `warning` | `W` |
| `Level.ERROR` | 4 | `error` | `E` |
| `Level.CRITICAL` | 5 | `critical` | `C` |
| `Level.OFF` | 6 | `off` | `O` |

```python
spd.Level.INFO < spd.Level.ERROR   # True
```

The values are spdlog's, not the `logging` module's (`logging.INFO` is 20). Use
`pyspdlog.level_from_logging()` to convert, see [12. The `logging` module](12-logging-module.md).

## Level names

Wherever a level is expected you can pass a case-insensitive name instead:

| Name | Level |
| --- | --- |
| `"trace"` | `TRACE` |
| `"debug"` | `DEBUG` |
| `"info"` | `INFO` |
| `"warn"`, `"warning"` | `WARN` |
| `"err"`, `"error"` | `ERROR` |
| `"critical"` | `CRITICAL` |
| `"off"` | `OFF` |

```python
log.set_level("Debug")
log.log("warning", "careful")
spd.level_from_str("error")    # Level.ERROR
spd.level_from_str("loud")     # ValueError: unknown log level 'loud'
```

## Two filters: the logger and the sink

A message is written to a sink only if its level is at least the **logger's level**
and at least that **sink's level**:

| | Default | Set with |
| --- | --- | --- |
| Logger level | `INFO` | `log.set_level(...)` or `log.level = ...` |
| Sink level | `TRACE` (lets everything through) | `sink.set_level(...)` or `sink.level = ...` |

```python
console = spd.StdoutColorSink(); console.set_level("warning")
file = spd.BasicFileSink("app.log")                 # TRACE: writes everything it gets

log = spd.Logger("app", [console, file])
log.set_level("debug")

log.debug("file only")
log.error("file and console")
```

`log.should_log(level)` and `sink.should_log(level)` report whether a message at
that level would pass. The logger's check is the one that skips formatting.

## Global level

`spd.set_level(level)` sets the level of every **registered** logger, including the
default logger. It also becomes the starting level of loggers created later by the
factories or `initialize_logger()`. It does not affect a `Logger(...)` you construct
yourself unless you pass it to `initialize_logger()`.

`spd.get_level()` returns the default logger's level (`OFF` if there is no default
logger), and `spd.should_log(level)` checks the default logger.

## Levels from the environment

`spd.load_env_levels()` reads spdlog's level syntax from an environment variable
(`SPDLOG_LEVEL` by default):

```sh
SPDLOG_LEVEL=info                    # every logger
SPDLOG_LEVEL=db=debug,http=off       # specific loggers
SPDLOG_LEVEL=warn,app=trace          # default for all, plus overrides
```

```python
spd.load_env_levels()                 # call it after creating your loggers, or before:
spd.load_env_levels("MYAPP_LOG")      # a different variable
```

The levels apply to the loggers registered at that moment and are remembered for
loggers the factories create later, matched by name.

---

[3. Messages](03-messages.md) · [Index](README.md) · Next: [5. Sinks](05-sinks.md)
