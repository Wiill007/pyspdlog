# 9. Async logging

An `AsyncLogger` splits the work between your thread and a background thread pool:

| Calling thread | Thread pool worker |
| --- | --- |
| level check, `str.format()`, copy into a queue | pattern formatting, sinks (I/O, Python callbacks) |

The call returns as soon as the message is queued, so a slow sink (a network share, a
Python callback, a disk that stalls) does not slow down the code that logs.

```python
log = spd.AsyncLogger("app", spd.BasicFileSink("app.log"))
log = spd.AsyncLogger("app", [sink1, sink2], overflow_policy=spd.OverflowPolicy.BLOCK)
log = spd.basic_logger_mt("app", "app.log", asynchronous=True)   # factories
```

`AsyncLogger` is a subclass of `Logger` with the same methods.

## The thread pool

All async loggers share a single thread pool. It is created the first time it is
needed, with room for 8192 messages and one worker thread. To change that, call
`init_thread_pool()` **before creating async loggers**:

```python
spd.init_thread_pool(queue_size=32768, thread_count=1)
```

`init_thread_pool()` replaces the pool. The old pool finishes the messages already in
its queue and stops. Async loggers created before the call stay bound to the old
pool, and everything they log afterwards fails with an error from spdlog on stderr
(`async log: thread pool doesn't exist anymore`). Create your async loggers after
setting the pool up.

**Ordering:** with one worker (the default), messages are written in the order they
were logged. With `thread_count > 1`, several workers process messages at the same
time, so messages can be written out of order, even from the same logger. Use more
than one worker only for independent, slow sinks.

## When the queue is full

| `OverflowPolicy` | Behavior |
| --- | --- |
| `BLOCK` (default) | wait until there is room. The GIL is released while waiting. |
| `OVERRUN_OLDEST` | drop the oldest queued message to make room |
| `DISCARD_NEW` | drop the new message |

The policy is per logger.

## Flushing

`log.flush()` on an async logger flushes its sinks **and waits until every message
queued so far has been processed**. spdlog's own C++ `flush()` only queues a flush
request; pyspdlog adds the wait so that, for example, a file is complete when
`flush()` returns.

Called from inside a sink running on the pool (for example a callback that calls
`log.flush()`), it does not wait, because the worker would be waiting for itself.

## When to use async

Async logging pays off when sinks are **slow or unpredictable**: network
filesystems, Python callbacks that do real work, disks under heavy load. It also
isolates the code that logs from stalls in the sinks.

For plain local files it is usually *not* faster. spdlog already buffers file writes,
and handing each message to another thread (queue, lock, wake-up) costs more than
the buffered write. See the numbers in [14. Performance](14-performance.md).

## Exit, shutdown and fork

- At exit, pyspdlog processes every queued message before stopping the pool.
- After `spd.shutdown()`, async loggers that existed before it drop messages
  quietly. Async loggers created afterwards get a new pool and work normally.
- Messages still queued are lost if the process ends without running `atexit`
  handlers (`os._exit()`, a signal that kills it, a crash). That includes
  `multiprocessing` children. Call `log.flush()` where it matters.
- After `os.fork()`, async loggers created in the parent drop their messages in the
  child, with a one-time `RuntimeWarning`.

The details are in [13. Threads, exit and fork](13-threads-exit-fork.md).

---

[8. Python sinks](08-python-sinks.md) · [Index](README.md) · Next: [10. Flushing](10-flushing.md)
