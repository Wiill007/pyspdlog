"""Interpreter exit: pending messages, background threads and reference cycles."""

from __future__ import annotations

import os
import textwrap

import pytest

from .conftest import run_python

CYCLES = {
    "closure": """
        log = spd.Logger("x", spd.CallbackSink(lambda m: log.name))
        log.info("hi")
    """,
    "module function": """
        def callback(m):
            log.name
        log = spd.Logger("x", spd.CallbackSink(callback))
    """,
    "registered async logger": """
        def callback(m):
            log.name
        log = spd.AsyncLogger("x", spd.CallbackSink(callback, flush=lambda: log))
        spd.register_logger(log)
        log.info("queued")
    """,
    "default logger with stream sink": """
        import sys
        log = spd.Logger("x", [spd.StreamSink(sys.stderr), spd.CallbackSink(lambda m: log)])
        spd.set_default_logger(log)
        log.set_pattern("%v")
        spd.info("to stderr")
    """,
}


@pytest.mark.parametrize("code", CYCLES.values(), ids=CYCLES.keys())
def test_reference_cycles_do_not_leak_at_exit(code):
    result = run_python("import pyspdlog as spd\n" + textwrap.dedent(code))
    assert result.returncode == 0, result.stderr
    assert "leaked" not in result.stderr
    assert result.stderr in ("", "to stderr\n")


def test_exit_while_a_daemon_thread_is_logging():
    # The daemon thread is frozen mid-call at exit, so the logger it uses is
    # never freed; that is expected, so nanobind's leak report stays off here.
    result = run_python(
        leak_warnings=False,
        code="""
        import threading
        import pyspdlog as spd

        log = spd.AsyncLogger("x", spd.CallbackSink(lambda m: None))

        def spam():
            while True:
                log.info("still here")

        threading.Thread(target=spam, daemon=True).start()
        """,
    )
    assert result.returncode == 0, result.stderr
    assert result.stderr == ""


def test_logging_from_atexit_handlers_registered_later():
    result = run_python(
        """
        import atexit
        import sys
        import pyspdlog as spd

        log = spd.Logger("x", spd.StreamSink(sys.stdout))
        log.set_pattern("%v")
        atexit.register(lambda: log.info("from atexit"))
        """
    )
    assert result.returncode == 0, result.stderr
    assert result.stdout == "from atexit\n"


def test_async_loggers_drop_messages_after_shutdown():
    result = run_python(
        """
        import pyspdlog as spd

        seen = []
        old = spd.AsyncLogger("old", spd.CallbackSink(lambda m: seen.append(m.payload)))
        old.info("before")
        spd.shutdown()
        old.info("dropped")
        old.clone("old-clone").info("dropped too")
        old.flush()
        new = spd.AsyncLogger("new", spd.CallbackSink(lambda m: seen.append(m.payload)))
        new.info("after")
        new.flush()
        print(seen)
        """
    )
    assert result.returncode == 0, result.stderr
    assert result.stdout == "['before', 'after']\n"
    assert result.stderr == ""


@pytest.mark.skipif(not hasattr(os, "fork"), reason="needs os.fork()")
def test_fork_with_background_threads(tmp_path):
    # The child inherits a leaked thread pool, which can keep Python objects
    # alive, so nanobind's leak report stays off.
    result = run_python(
        leak_warnings=False,
        code=f"""
        import os, sys, warnings
        warnings.simplefilter("ignore", DeprecationWarning)  # fork() with threads, 3.12+
        import pyspdlog as spd

        parent = spd.AsyncLogger("parent", spd.BasicFileSink({str(tmp_path / "parent.log")!r}))
        parent.set_pattern("%v")
        spd.register_logger(parent)
        spd.flush_every(0.001)
        parent.info("before fork")
        parent.flush()

        pid = os.fork()
        if pid == 0:
            with warnings.catch_warnings(record=True) as caught:
                warnings.simplefilter("always")
                # More than the queue holds: this would block forever if the
                # messages were still queued for the parent's threads.
                for i in range(20_000):
                    parent.info("from child {{}}", i)
                parent.flush()
            assert [w.category for w in caught] == [RuntimeWarning], caught
            child = spd.AsyncLogger("child", spd.BasicFileSink({str(tmp_path / "child.log")!r}))
            child.set_pattern("%v")
            child.info("child logger")
            child.flush()
            spd.flush_every(0.001)
            sys.exit(0)  # runs atexit handlers, which must not wait for the parent's threads

        _, status = os.waitpid(pid, 0)
        parent.info("after fork")
        parent.flush()
        print(os.waitstatus_to_exitcode(status))
        """,
    )
    assert result.returncode == 0, result.stderr
    assert result.stdout == "0\n"
    assert result.stderr == ""
    assert (tmp_path / "parent.log").read_text().splitlines() == ["before fork", "after fork"]
    assert (tmp_path / "child.log").read_text() == "child logger\n"


def test_lifecycle_functions_refuse_to_run_on_background_threads():
    import threading

    import pyspdlog as spd

    errors = []

    def call_all():
        for fn in (spd.shutdown, spd.init_thread_pool, lambda: spd.flush_every(1)):
            try:
                fn()
            except RuntimeError as e:
                errors.append(str(e))

    log = spd.AsyncLogger("guard", spd.CallbackSink(lambda m: call_all()))
    log.info("on the thread pool")
    log.flush()
    assert len(errors) == 3
    assert all("background thread" in e for e in errors)

    errors.clear()
    flushed = threading.Event()

    def flush():
        call_all()
        flushed.set()

    spd.register_logger(spd.Logger("flushing", spd.CallbackSink(lambda m: None, flush=flush)))
    spd.flush_every(0.001)
    assert flushed.wait(timeout=10)
    spd.flush_every(0)
    assert errors[:3] and all("background thread" in e for e in errors)
