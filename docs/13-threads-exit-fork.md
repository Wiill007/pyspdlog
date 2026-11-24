# 13. Threads, exit and fork

This page explains what pyspdlog does with threads, what happens when the
interpreter exits, and how it behaves across `os.fork()`. Most programs never need
to think about it, but it explains the edge cases.

## Threads and the GIL

- Every logger and sink is thread-safe. Log from as many threads as you like.
- spdlog's work runs **with the GIL released**: waiting for locks, pattern formatting
  and I/O. Only the level check and `str.format()` of your message run with the GIL.
  Other Python threads keep running while a message is written.
- Python sinks (`CallbackSink`, `StreamSink`) take the GIL for the duration of the
  Python call, like any Python code.

### Why it cannot deadlock

spdlog holds its own locks (registry, sinks, async queue, backtrace) while it runs
sinks, and Python sinks need the GIL. pyspdlog keeps two rules everywhere:

1. It never waits for a spdlog lock while holding the GIL. Every operation that may
   lock releases the GIL first.
2. Python sinks never hold a lock while running Python code.

So no thread ever holds a lock while waiting for the GIL that another thread holds
while waiting for that lock. This is also why a callback can log, even to its own
logger.

## Background threads

pyspdlog can start two kinds of background threads:

| Thread | Started by | Does |
| --- | --- | --- |
| thread pool workers | the first `AsyncLogger`, or `init_thread_pool()` | run the sinks of async loggers |
| periodic flusher | `flush_every(interval)` | flushes registered loggers every `interval` |

A sink running on one of those threads must not call `shutdown()`,
`init_thread_pool()` or `flush_every()`. Each of them would have to wait for the
current thread to finish, which can never happen. They raise `RuntimeError: ...
cannot be called from a sink running on a background thread` instead.
`AsyncLogger.flush()` called from a pool worker flushes without waiting.

## What happens at exit

When pyspdlog is imported it registers an `atexit` handler. At normal interpreter
exit (end of the script, `sys.exit()`, an uncaught exception), the handler:

1. stops the periodic flusher;
2. calls `shutdown()`: every logger is flushed and dropped from the registry, and the
   thread pool processes the messages still queued and stops;
3. releases the Python objects (callbacks and streams) held by Python sinks, to break
   reference cycles (see [below](#reference-cycles)).

Afterwards:

| | Behavior |
| --- | --- |
| module-level functions (`spd.info()`, ...) | do nothing (there is no default logger) |
| a regular logger you still hold | works, except its Python sinks |
| an `AsyncLogger` created before the shutdown | drops messages quietly |
| Python sinks | do nothing |

`atexit` runs handlers in reverse order of registration. Handlers registered *after*
`import pyspdlog` run before this cleanup and can log normally. Handlers registered
before the import run after it.

### When `atexit` does not run

`os._exit()`, a process killed by a signal, and a crash skip `atexit`. `multiprocessing`
child processes end with `os._exit()` too. In those cases buffered file output and
queued async messages are lost unless you flushed them. Call `log.flush()` at the end
of a worker function, or use `flush_on()` / `flush_every()`
([10. Flushing](10-flushing.md)).

### Daemon threads

Daemon threads can still be running while the interpreter exits. They can keep
logging during the cleanup: regular loggers keep working, and async loggers drop
their messages quietly instead of printing errors.

Python then freezes daemon threads mid-call. Any object they were using is never
freed, which is harmless at exit.

## Fork

Threads do not survive `os.fork()`: the child process only has the thread that called
`fork()`. If the parent had a thread pool or a periodic flusher, the child inherits
the C++ objects that manage them, but not their threads. Without special handling:

- an async logger inherited from the parent would queue messages that nobody
  processes, and block forever once the queue is full;
- `shutdown()` at exit would wait forever for threads that do not exist.

pyspdlog registers `os.register_at_fork()` handlers to avoid both:

- **Before the fork**, it waits for the periodic flusher to finish its current flush
  and holds it, so the child does not inherit a lock in use.
- **In the child**, it abandons the inherited thread pool and flusher without
  touching them. Async loggers created before the fork **drop their messages**, and
  the first one to do so issues a `RuntimeWarning`. New async loggers created in the
  child get a new thread pool and work normally, and `flush_every()` can be called
  again.
- **Regular (synchronous) loggers work in the child.** They share open files with
  the parent, so lines from both processes can interleave in the same file.

```python
import multiprocessing as mp

def worker(n):
    log = spd.AsyncLogger(f"worker-{n}", spd.BasicFileSink(f"worker-{n}.log"))  # created in the child
    log.info("hello from {}", n)
    log.flush()                     # the child exits with os._exit(): flush yourself

if __name__ == "__main__":
    ctx = mp.get_context("spawn")   # or "forkserver"; avoids fork problems altogether
    with ctx.Pool(4) as pool:
        pool.map(worker, range(4))
```

Recommendations:

- Prefer the `spawn` or `forkserver` start methods for `multiprocessing`. Python 3.14
  already uses `forkserver` by default on Linux.
- With `fork`, create async loggers in the child, not before forking.
- Since Python 3.12, forking a process that has threads (such as the thread pool)
  issues a `DeprecationWarning` from Python itself. That warning is expected.

Fork handling is best effort. A lock held by *another* thread of yours at the moment
of the fork, for example in the middle of writing to a file, stays locked in the
child. That applies to any multi-threaded C or C++ library.

## Reference cycles

A sink keeps its Python callback alive, and a logger keeps its sinks alive. Those
links go through C++, where Python's garbage collector cannot see them. A callback
that refers back to its logger therefore forms a cycle the collector cannot free:

```python
log = spd.Logger("x", spd.CallbackSink(lambda m: log.flush()))   # log → sink → lambda → log
```

That also happens indirectly: a module-level function used as a callback refers to the
module's globals, which contain the module-level logger.

For long-lived loggers this does not matter, since they live until exit anyway, and
at exit pyspdlog releases every Python sink's callback, which breaks the cycles. For
loggers you create and throw away repeatedly, avoid referring to the logger from its
callback, or hold it through `weakref.ref`.

## Diagnosing leaks

nanobind can report C++-backed objects that are still alive when the interpreter
exits. pyspdlog turns that report off by default, because objects used by daemon
threads always show up in it. Set `PYSPDLOG_LEAK_WARNINGS=1` to turn it on:

```sh
PYSPDLOG_LEAK_WARNINGS=1 python app.py
# nanobind: leaked 2 instances! ...
```

## Free-threaded Python

Free-threaded CPython builds (3.13t, 3.14t) are not supported. The extension is not
declared free-threading-safe and no wheels are built for them.

---

[12. The logging module](12-logging-module.md) · [Index](README.md) · Next: [14. Performance](14-performance.md)
