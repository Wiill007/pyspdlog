# 12. The `logging` module

Most libraries log through Python's standard `logging` module. `SpdlogHandler`
sends those records to spdlog, so everything ends up in the same sinks with the
same format.

## `SpdlogHandler`

```python
spd.SpdlogHandler(logger=None, level=logging.NOTSET)
```

`logger` selects where records go:

| `logger` | Records go to |
| --- | --- |
| `None` (default) | the default logger |
| `"name"` | the registered logger with that name |
| a `Logger` | that logger |

A name, or the default logger, is looked up for **every record**. You can therefore
replace the logger while the handler is in use, or create it after the handler.
While no such logger exists, records are dropped silently. `handler.target` returns
the logger records currently go to.

### Typical setups

Send everything to the default logger:

```python
import logging
import pyspdlog as spd

logging.basicConfig(
    level=logging.INFO,
    format="%(name)s: %(message)s",   # see the note below
    handlers=[spd.SpdlogHandler()],
)
logging.getLogger("urllib3").warning("retrying")
# [2026-01-31 12:00:00.000] [warning] urllib3: retrying
```

Send one library to its own spdlog logger:

```python
spd.rotating_logger_mt("sql", "logs/sql.log", 10_000_000, 3)

sql = logging.getLogger("sqlalchemy.engine")
sql.addHandler(spd.SpdlogHandler("sql"))
sql.setLevel(logging.INFO)
sql.propagate = False
```

## How a record is converted

1. The record's level is mapped to a spdlog `Level`:

   | `logging` level | spdlog level |
   | --- | --- |
   | `CRITICAL` (50) and above | `CRITICAL` |
   | `ERROR` (40) to 49 | `ERROR` |
   | `WARNING` (30) to 39 | `WARN` |
   | `INFO` (20) to 29 | `INFO` |
   | `DEBUG` (10) to 19 | `DEBUG` |
   | below 10 | `TRACE` |

   Custom levels round down, so 25 becomes `INFO`. The function is available as
   `spd.level_from_logging(levelno)`.

2. If the spdlog logger would drop that level (and has no backtrace), the record is
   skipped before it is formatted.

3. The handler's `logging.Formatter` renders the record. Without a formatter set,
   that is `%(message)s`, plus the traceback when there is `exc_info`.

4. The resulting text is logged as-is (it is not formatted a second time, so braces
   in it are fine). spdlog's pattern adds the time, logger name and level around it.

> **`basicConfig()` and duplicated levels.** `logging.basicConfig()` gives the
> handlers you pass a formatter with its default format,
> `%(levelname)s:%(name)s:%(message)s`. The spdlog pattern already shows the level,
> so pass `format=` (as above) to avoid `[warning] WARNING:urllib3:retrying`.

### Filtering

A record has to pass three filters: the `logging` logger's level, the handler's
`level` argument, and the spdlog logger's level (and its sinks' levels).

### Source location

spdlog's `%s`/`%#` pattern flags are empty in pyspdlog. If you want the Python file
and line, put them in the formatter:

```python
handler = spd.SpdlogHandler()
handler.setFormatter(logging.Formatter("%(filename)s:%(lineno)d %(message)s"))
```

### Errors

Errors while handling a record, such as a bad `%` format, go to the handler's
`handleError()`, as with every `logging` handler. With the default
`logging.raiseExceptions = True`, that prints `--- Logging error ---` to stderr.

## The other direction: spdlog to `logging`

To route spdlog messages *into* `logging`, for example because another tool
collects `logging` output, use a `CallbackSink`:

```python
import logging

TO_LOGGING = {
    spd.Level.TRACE: 5, spd.Level.DEBUG: logging.DEBUG, spd.Level.INFO: logging.INFO,
    spd.Level.WARN: logging.WARNING, spd.Level.ERROR: logging.ERROR,
    spd.Level.CRITICAL: logging.CRITICAL,
}

def forward(msg: spd.LogMessage) -> None:
    logging.getLogger(msg.logger_name or None).log(TO_LOGGING[msg.level], msg.payload)

log = spd.Logger("app", spd.CallbackSink(forward))
```

Do not combine this with a `SpdlogHandler` that sends the same records back, or
each message loops between the two.

---

[11. Backtrace](11-backtrace.md) · [Index](README.md) · Next: [13. Threads, exit and fork](13-threads-exit-fork.md)
