from __future__ import annotations

import threading

import pytest

import pyspdlog as spd

from .conftest import run_python


@pytest.fixture
def default_pool():
    """Restore the default single-threaded pool after tests that replace it."""
    yield
    spd.init_thread_pool()


def callback_logger(name: str = "async", **kwargs) -> tuple[spd.AsyncLogger, list[spd.LogMessage]]:
    messages: list[spd.LogMessage] = []
    log = spd.AsyncLogger(name, spd.CallbackSink(messages.append), **kwargs)
    return log, messages


def test_messages_are_processed_on_a_worker_thread():
    threads = []
    log = spd.AsyncLogger("worker", spd.CallbackSink(lambda m: threads.append(threading.get_ident())))
    log.info("x")
    log.flush()
    assert len(threads) == 1
    assert threads[0] != threading.get_ident()


def test_flush_waits_for_everything_queued():
    log, messages = callback_logger()
    for i in range(2000):
        log.info("{}", i)
    log.flush()
    assert [m.payload for m in messages] == [str(i) for i in range(2000)]


def test_async_logger_basics(ring):
    log = spd.AsyncLogger("basics", [ring], overflow_policy=spd.OverflowPolicy.BLOCK)
    log.set_pattern("%l|%v")
    assert isinstance(log, spd.Logger)
    assert repr(log) == "<AsyncLogger 'basics' level=info>"
    log.warning("w {}", 1)
    log.flush()
    assert ring.last_formatted() == ["warning|w 1"]


def test_registry_returns_the_async_logger():
    log, _ = callback_logger("registered")
    spd.register_logger(log)
    assert spd.get("registered") is log
    assert isinstance(log.clone("cloned"), spd.AsyncLogger)


def test_many_python_threads(default_pool):
    spd.init_thread_pool(queue_size=1024, thread_count=4)
    log, messages = callback_logger()

    def work(n: int) -> None:
        for i in range(500):
            log.info("{}-{}", n, i)

    threads = [threading.Thread(target=work, args=(n,)) for n in range(6)]
    for t in threads:
        t.start()
    for t in threads:
        t.join()
    log.flush()
    assert sorted(m.payload for m in messages) == sorted(f"{n}-{i}" for n in range(6) for i in range(500))


def test_flush_from_a_worker_thread_does_not_deadlock():
    done = threading.Event()

    def callback(msg):
        log.flush()  # runs on the pool's worker thread
        done.set()

    log = spd.AsyncLogger("self-flush", spd.CallbackSink(callback))
    log.info("x")
    assert done.wait(timeout=10)
    log.flush()


def _fill_queue_while_blocked(policy: spd.OverflowPolicy) -> list[str]:
    spd.init_thread_pool(queue_size=4, thread_count=1)
    release = threading.Event()
    started = threading.Event()
    payloads: list[str] = []

    def callback(msg):
        if msg.payload == "0":
            started.set()
            release.wait(timeout=10)
        payloads.append(msg.payload)

    log = spd.AsyncLogger("overflow", spd.CallbackSink(callback), overflow_policy=policy)
    log.info("0")
    assert started.wait(timeout=10)  # the worker is now stuck on message 0
    for i in range(1, 50):
        log.info("{}", i)
    release.set()
    log.flush()
    return payloads


def test_discard_new_drops_new_messages(default_pool):
    payloads = _fill_queue_while_blocked(spd.OverflowPolicy.DISCARD_NEW)
    assert payloads[0] == "0"
    assert len(payloads) < 50
    assert payloads[1:] == [str(i) for i in range(1, len(payloads))]


def test_overrun_oldest_keeps_newest_messages(default_pool):
    payloads = _fill_queue_while_blocked(spd.OverflowPolicy.OVERRUN_OLDEST)
    assert payloads[0] == "0"
    assert len(payloads) < 50
    assert payloads[-1] == "49"


def test_init_thread_pool_validates_arguments(default_pool):
    with pytest.raises(ValueError):
        spd.init_thread_pool(queue_size=0)
    with pytest.raises(spd.SpdlogError):
        spd.init_thread_pool(thread_count=0)


def test_no_deadlock_between_registry_calls_and_python_sinks():
    """Periodic flusher + Python flush callback + registry calls from Python threads."""
    flushes = []
    log = spd.Logger("busy", spd.CallbackSink(lambda m: None, flush=lambda: flushes.append(1)))
    spd.register_logger(log)
    async_log, _ = callback_logger("busy-async")
    spd.register_logger(async_log)
    spd.flush_every(0.001)
    stop = threading.Event()

    def registry_user():
        while not stop.is_set():
            spd.get("busy")
            spd.all_loggers()
            spd.set_level("info")

    def logger_user():
        while not stop.is_set():
            log.info("x")
            async_log.info("y")

    threads = [threading.Thread(target=f) for f in (registry_user, registry_user, logger_user)]
    for t in threads:
        t.start()
    stop.wait(timeout=0.5)
    stop.set()
    for t in threads:
        t.join(timeout=30)
        assert not t.is_alive(), "deadlock"
    async_log.flush()
    assert flushes


def test_pending_messages_are_written_at_exit(tmp_path):
    out = tmp_path / "out.txt"
    result = run_python(
        f"""
        import pyspdlog as spd
        f = open({str(out)!r}, "w")
        log = spd.AsyncLogger("exit", spd.CallbackSink(lambda m: f.write(m.payload + "\\n")))
        spd.register_logger(log)
        spd.flush_every(0.001)
        for i in range(5000):
            log.info("{{}}", i)
        """
    )
    assert result.returncode == 0, result.stderr
    assert result.stderr == ""
    assert out.read_text().splitlines() == [str(i) for i in range(5000)]


def test_unregistered_async_logger_at_exit():
    result = run_python(
        """
        import pyspdlog as spd
        seen = []
        log = spd.AsyncLogger("unregistered", spd.CallbackSink(seen.append))
        for i in range(100):
            log.info("x")
        """
    )
    assert result.returncode == 0, result.stderr
    assert result.stderr == ""


def test_flush_after_shutdown_is_silent():
    result = run_python(
        """
        import pyspdlog as spd
        log = spd.AsyncLogger("late", spd.NullSink())
        spd.shutdown()
        log.flush()
        print("ok")
        """
    )
    assert result.returncode == 0, result.stderr
    assert result.stdout == "ok\n"
    assert result.stderr == ""
