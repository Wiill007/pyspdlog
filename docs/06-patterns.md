# 6. Patterns

A pattern is a template for each output line, made of text and `%` flags.

```python
log.set_pattern("[%H:%M:%S.%e] [%n] [%^%l%$] %v")
log.info("ready")
# [12:00:00.123] [app] [info] ready
```

## Where patterns apply

| Call | Applies to |
| --- | --- |
| `sink.set_pattern(p)` | that sink |
| `log.set_pattern(p)` | every sink of that logger (replacing their patterns) |
| `spd.set_pattern(p)` | every registered logger, and loggers created later by factories / `initialize_logger()` |

Each of them accepts a second argument, `time_type`, except `sink.set_pattern()`.
Pass `spd.PatternTimeType.UTC` to print times in UTC instead of local time
(`PatternTimeType.LOCAL`, the default):

```python
log.set_pattern("%Y-%m-%dT%H:%M:%S.%fZ %v", spd.PatternTimeType.UTC)
```

## The default pattern

The default is `%+`, which renders as:

```text
[2026-01-31 12:00:00.123] [app] [info] message
```

That is `[%Y-%m-%d %H:%M:%S.%e] [%n] [%l] %v` with the level colored. The `[%n]`
part is left out for loggers with an empty name, such as the default logger.

## Flags

| Flag | Meaning | Example |
| --- | --- | --- |
| `%v` | the message | `ready` |
| `%n` | logger name | `app` |
| `%l` | level | `info` |
| `%L` | short level | `I` |
| `%t` | OS thread id of the thread that logged | `2711506` |
| `%P` | process id | `2711506` |
| `%Y` | year | `2026` |
| `%C` | 2-digit year | `26` |
| `%m` | month 01–12 | `11` |
| `%d` | day 01–31 | `24` |
| `%H` | hour 00–23 | `15` |
| `%I` | hour 01–12 | `03` |
| `%M` | minute | `45` |
| `%S` | second | `08` |
| `%e` | milliseconds | `531` |
| `%f` | microseconds | `531413` |
| `%F` | nanoseconds | `531416688` |
| `%p` | AM/PM | `PM` |
| `%a` / `%A` | weekday name, short / full | `Tue` / `Tuesday` |
| `%b` / `%B` | month name, short / full | `Nov` / `November` |
| `%c` | date and time | `Tue Nov 24 15:45:08 2026` |
| `%D` | short date MM/DD/YY | `11/24/26` |
| `%R` | HH:MM | `15:45` |
| `%T` | HH:MM:SS (ISO 8601) | `15:45:08` |
| `%r` | 12-hour clock | `03:45:08 PM` |
| `%z` | UTC offset | `+01:00` |
| `%E` | seconds since the epoch | `1795531508` |
| `%o` / `%i` / `%u` / `%O` | time since the previous message: ms / µs / ns / s | `0` |
| `%^` / `%$` | start / end of the colored range (color sinks only) | |
| `%+` | the default pattern | |
| `%%` | a literal `%` | `%` |

### Source location flags are empty

spdlog also has `%@` (file:line), `%s` (file name), `%g` (full path), `%#` (line) and
`%!` (function). pyspdlog does not pass Python source locations to spdlog, so these
are always empty. When you log through the `logging` module, put
`%(filename)s:%(lineno)d` in the handler's `Formatter` instead (see
[12. The `logging` module](12-logging-module.md)).

## Alignment and truncation

Put a width between `%` and the flag:

| Pattern | Output for `warning` |
| --- | --- |
| `[%8l]` | `[ warning]` (right-aligned) |
| `[%-8l]` | `[warning ]` (left-aligned) |
| `[%=8l]` | `[warning ]` (centered) |
| `[%3!l]` | `[war]` (truncated to 3) |

Aligned columns make logs easier to scan:

```python
log.set_pattern("%H:%M:%S.%e %-8l %-10n %v")
```

## Colors

In `StdoutColorSink` / `StderrColorSink`, the text between `%^` and `%$` gets the
color of the message's level. Without the markers, nothing is colored.

```python
log.set_pattern("%^[%l]%$ %v")        # color the level tag
log.set_pattern("%^%v%$")             # color the whole message
```

Other sinks ignore the markers.

## Reference

The pattern syntax is spdlog's. Its wiki page
[Custom formatting](https://github.com/gabime/spdlog/wiki/Custom-formatting) has
more examples. Custom flag formatters (`custom_flag_formatter` in C++) are not
available from Python.

---

[5. Sinks](05-sinks.md) · [Index](README.md) · Next: [7. The registry](07-registry.md)
