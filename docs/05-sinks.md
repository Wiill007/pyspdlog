# 5. Sinks

A *sink* is a destination. A logger sends each message that passes its level to all
of its sinks, and each sink applies its own level and pattern before writing.

All sinks are thread-safe, and one sink can be shared by several loggers:

```python
shared = spd.BasicFileSink("all.log")
db = spd.Logger("db", shared)
http = spd.Logger("http", [shared, spd.StdoutColorSink()])
```

A logger's sinks are fixed when it is created. `log.sinks` returns a copy of the
list. To change destinations, create a new logger, which can reuse the same sinks.

## Choosing a sink

| You want | Use |
| --- | --- |
| Readable console output | `StdoutColorSink` / `StderrColorSink` |
| Console output for pipes or log collectors | `StdoutSink` / `StderrSink` |
| One log file | `BasicFileSink` |
| Files that never grow beyond a size | `RotatingFileSink` |
| One file per day | `DailyFileSink` |
| The last N messages in memory (tests, crash reports) | `RingbufferSink` |
| Output in Jupyter, or into any Python stream | `StreamSink` ([8](08-python-sinks.md)) |
| Your own Python code for each message | `CallbackSink` ([8](08-python-sinks.md)) |
| To discard everything | `NullSink` |

## Console sinks

```python
spd.StdoutColorSink(color_mode=spd.ColorMode.AUTOMATIC)
spd.StderrColorSink(color_mode=spd.ColorMode.AUTOMATIC)
spd.StdoutSink()
spd.StderrSink()
```

| `ColorMode` | Colors |
| --- | --- |
| `AUTOMATIC` (default) | only when writing to a terminal that supports colors (checked through `TERM` / `COLORTERM`) |
| `ALWAYS` | always, even into pipes and files |
| `NEVER` | never |

The colored part of the line is set by the pattern's `%^...%$` markers. The default
pattern colors the level name. See [6. Patterns](06-patterns.md).

Console sinks flush after every message. They write straight to the process's file
descriptors 1 and 2, **not** through `sys.stdout`/`sys.stderr`. As a consequence:

- Jupyter notebooks do not show their output (it goes to the kernel's terminal).
- `contextlib.redirect_stdout()` and pytest's `capsys` do not capture it (pytest's
  `capfd` does).

Use `StreamSink()` in those cases.

## `BasicFileSink`: one file

```python
spd.BasicFileSink(filename, truncate=False)
```

- `filename` is a `str` or a path-like object. Missing parent directories are created.
- Messages are appended to an existing file, unless `truncate=True` empties it first.
- `sink.filename` returns the path. `sink.truncate()` empties the file at any time.
- If the file cannot be opened, the constructor raises `SpdlogError`.

## `RotatingFileSink`: rotation by size

```python
spd.RotatingFileSink(filename, max_size, max_files, rotate_on_open=False)
```

When writing a message would push the file past `max_size` bytes, the files are
shifted and a new one is started. At most `max_files` old files are kept:

```text
app.log      ← current
app.1.log    ← previous
app.2.log    ← older (with max_files=2 this is the oldest kept)
```

The index goes before the extension (`app.1.log`). A file without an extension gets it
at the end (`app.1`).

- `rotate_on_open=True` rotates once when the sink is created, so every run starts a
  new file.
- `sink.rotate_now()` rotates immediately. `sink.filename` is the current file.
- `max_size` must be greater than 0 (otherwise `SpdlogError`).

## `DailyFileSink`: one file per day

```python
spd.DailyFileSink(filename, rotation_hour=0, rotation_minute=0, truncate=False, max_files=0)
```

The date is inserted before the extension: `logs/app.log` becomes
`logs/app_2026-01-31.log`. A new file starts every day at
`rotation_hour:rotation_minute` (local time).

- `max_files > 0` keeps only that many files and deletes older ones. `0` keeps all.
- `sink.filename` is the file currently being written.
- An invalid time (hour outside 0–23, minute outside 0–59) raises `SpdlogError`.

## `RingbufferSink`: in memory

```python
sink = spd.RingbufferSink(n_items)       # n_items > 0
sink.last_formatted(limit=0)             # list[str], oldest first, without newlines
```

The sink keeps the last `n_items` messages. `limit > 0` returns only the newest
`limit` of them. It is handy in tests and for attaching recent log lines to crash
reports.

> **Note:** `RingbufferSink` stores the raw messages and formats them **when you call
> `last_formatted()`**, using the sink's pattern *at that moment*. Changing the
> pattern after logging changes how all stored messages are rendered. Elapsed-time
> flags (`%o`, `%i`, `%u`, `%O`) are meaningless there.

## `NullSink`

Discards everything. Useful to silence a logger that some code requires.

## Sink methods

Every sink, including the Python ones, has:

| | |
| --- | --- |
| `sink.level` / `sink.set_level(level)` | the sink's minimum level (default `TRACE`) |
| `sink.should_log(level)` | whether the sink would write a message at `level` |
| `sink.set_pattern(pattern)` | the sink's output format |
| `sink.flush()` | write out anything buffered |

> **Patterns get replaced.** `logger.set_pattern()` sets the pattern of *all* of that
> logger's sinks. The factories, `initialize_logger()` and the global
> `spd.set_pattern()` also overwrite the patterns of the sinks involved. Set
> per-sink patterns last.

## Buffering

File sinks buffer their output. The console sinks flush on every message. See
[10. Flushing](10-flushing.md) for how to control when data reaches the disk.

---

[4. Levels](04-levels.md) · [Index](README.md) · Next: [6. Patterns](06-patterns.md)
