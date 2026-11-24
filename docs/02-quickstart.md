# 2. Quick start

All examples import the package as `spd`:

```python
import pyspdlog as spd
```

## Log right away

The module-level functions log through the *default logger*, which writes colored
output to stdout:

```python
spd.info("server started")
spd.warning("disk {:.0%} full", 0.93)
```

```text
[2026-01-31 12:00:00.000] [info] server started
[2026-01-31 12:00:00.000] [warning] disk 93% full
```

## Named loggers

Real programs usually have one logger per component. A *factory* creates a logger,
gives it a sink and registers it under its name:

```python
log = spd.stdout_color_mt("db")
log.info("connected to {}", "postgres://localhost")
# [2026-01-31 12:00:00.000] [db] [info] connected to postgres://localhost

spd.get("db") is log  # True: anywhere in the program
```

## Levels

A logger drops messages below its level, which is `INFO` by default:

```python
log.debug("not shown")
log.set_level("debug")       # or spd.Level.DEBUG
log.debug("shown now")
```

## Logging to a file

```python
log = spd.basic_logger_mt("app", "logs/app.log")  # creates logs/ if needed
log.info("written to the file")
```

File output is buffered. It reaches the disk when the logger is flushed, at the
latest when the program exits. To write immediately at important levels:

```python
log.flush_on("error")
```

## Several destinations at once

Build the logger yourself to combine sinks. Each sink can have its own level and
pattern:

```python
console = spd.StdoutColorSink()
console.set_level("warning")                   # only warnings and above on screen

file = spd.RotatingFileSink("logs/app.log", max_size=5_000_000, max_files=3)
file.set_level("debug")

log = spd.Logger("app", [console, file])
log.set_level("debug")
spd.register_logger(log)                       # optional
```

## Using the `logging` module too

Send records from libraries that use `logging` into spdlog:

```python
import logging

logging.basicConfig(level=logging.INFO, format="%(name)s: %(message)s",
                    handlers=[spd.SpdlogHandler()])
```

## That's it

Exit normally and pyspdlog flushes everything for you. The next pages cover each
piece in detail.

---

[1. Installation](01-installation.md) · [Index](README.md) · Next: [3. Messages](03-messages.md)
