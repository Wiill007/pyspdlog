# 11. Backtrace

With backtrace enabled, a logger remembers its last *N* messages **of any level**,
including the ones its level filters out. `dump_backtrace()` logs them on demand.
You get the DEBUG context leading up to a problem without logging DEBUG all the time.

```python
log = spd.stdout_color_mt("worker")      # level INFO
log.enable_backtrace(32)

for item in batch:
    log.debug("processing {}", item.id)  # not printed, but remembered
    try:
        process(item)
    except Exception:
        log.exception("item {} failed", item.id)
        log.dump_backtrace()              # print the last 32 messages
```

```text
[12:00:00.003] [worker] [error] item 17 failed
Traceback (most recent call last): ...
[12:00:00.003] [worker] [info] ****************** Backtrace Start ******************
[12:00:00.001] [worker] [debug] processing 16
[12:00:00.002] [worker] [debug] processing 17
[12:00:00.003] [worker] [error] item 17 failed ...
[12:00:00.003] [worker] [info] ****************** Backtrace End ********************
```

Each remembered message is printed with its original time and level. Messages that
pass the level are logged normally *and* also remembered.

## API

| Call | Does |
| --- | --- |
| `log.enable_backtrace(n)` | remember the last `n` messages |
| `log.disable_backtrace()` | stop remembering and forget the stored messages |
| `log.dump_backtrace()` | log the stored messages between Start/End markers |
| `log.should_backtrace()` | whether backtrace is enabled |
| `spd.enable_backtrace(n)` / `spd.disable_backtrace()` | for every registered logger and for loggers created later |
| `spd.dump_backtrace()` | dump the default logger's backtrace (no-op without one) |

## Cost

A message that is only remembered still has to be **formatted**. With backtrace
enabled, `log.debug("{}", obj)` calls `obj.__format__` even though the level is
INFO. Lazy formatting does not apply. Enable backtrace on loggers where that extra
work is acceptable.

---

[10. Flushing](10-flushing.md) · [Index](README.md) · Next: [12. The logging module](12-logging-module.md)
