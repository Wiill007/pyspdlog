# 3. Messages

Every logging method takes a message followed by optional arguments:

```python
log.info(msg, *args, **kwargs)
log.log(level, msg, *args, **kwargs)
spd.info(msg, *args, **kwargs)   # through the default logger
```

## `str.format()` syntax

With arguments, the message is formatted as `msg.format(*args, **kwargs)`, so the
full [format specification](https://docs.python.org/3/library/string.html#formatstrings)
is available:

```python
log.info("user {} logged in", name)
log.info("from {ip} after {tries} tries", ip=addr, tries=3)
log.info("{0} paid {1:.2f} €, {0} is happy", "Ana", 12.5)
log.info("{:>10}|{:<10}|", "right", "left")
log.info("progress {:.1%}", 0.734)
```

This is the same `{}` style spdlog uses in C++. It is different from the `logging`
module, whose messages use `%s`.

Keyword arguments can have any name, including `msg`, `level` and `args`, because
`msg` and `level` are positional-only:

```python
log.info("battery at {level}%", level=80)
```

## Formatting is lazy

The message is only formatted if it will actually be logged, meaning the level is
enabled (or [backtrace](11-backtrace.md) is on). A disabled `debug()` call costs about
as much as a call to an empty Python function:

```python
log.set_level("info")
log.debug("state: {}", big_object)  # big_object.__format__ is never called
```

Python still evaluates the *arguments* before the call, though. Guard expensive
computations explicitly:

```python
if log.should_log("debug"):
    log.debug("histogram: {}", compute_histogram())
```

Prefer arguments over f-strings for messages that may be disabled.
`log.debug(f"state: {big_object}")` formats the object even when DEBUG is off.

## Messages without arguments are used as-is

Without arguments, nothing is formatted, so braces need no escaping:

```python
log.info("literal {} and {name}")      # logs: literal {} and {name}
log.info("{{}} with {}", 1)            # with arguments, double the braces: {} with 1
```

## Non-string messages

Anything that is not a `str` is converted with `str()`:

```python
log.info(42)          # 42
log.info(None)        # None
log.info(some_obj)    # str(some_obj)
```

## Formatting errors raise

A message that does not match its arguments raises the usual `str.format()` error
from the logging call:

```python
log.info("{} and {}", 1)      # IndexError
log.info("{missing}", 1)      # KeyError
```

This only happens when the message is enabled. The `logging` module behaves
differently: it prints a `--- Logging error ---` report and carries on.

## Logging exceptions

`exception()` logs at `ERROR` and appends the traceback of the exception currently
being handled:

```python
try:
    connect(host)
except OSError:
    log.exception("cannot connect to {}", host)
```

```text
[2026-01-31 12:00:00.000] [app] [error] cannot connect to db1
Traceback (most recent call last):
  File "app.py", line 12, in <module>
    connect(host)
OSError: [Errno 111] Connection refused
```

Outside an `except` block it logs just the message. `spd.exception()` does the same
through the default logger.

## Text encoding

Messages are UTF-8 inside spdlog and come back out as UTF-8 (in files, on the
console and in Python sinks). Any Python string is fine, including emoji and
non-Latin scripts.

---

[2. Quick start](02-quickstart.md) · [Index](README.md) · Next: [4. Levels](04-levels.md)
