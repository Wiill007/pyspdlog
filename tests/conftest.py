from __future__ import annotations

import os
import subprocess
import sys
import textwrap

import pytest

import pyspdlog as spd


@pytest.fixture(autouse=True)
def _reset_spdlog():
    """Give every test a clean registry and default global settings."""
    original_default = spd.default_logger()
    yield
    spd.flush_every(0)
    spd.drop_all()
    spd.set_default_logger(original_default)
    spd.set_automatic_registration(True)
    spd.set_level(spd.Level.INFO)
    spd.set_pattern("%+")
    spd.flush_on(spd.Level.OFF)
    spd.disable_backtrace()


@pytest.fixture
def ring() -> spd.RingbufferSink:
    return spd.RingbufferSink(1000)


@pytest.fixture
def logger(ring: spd.RingbufferSink) -> spd.Logger:
    """An unregistered logger whose output is "<level>|<message>"."""
    log = spd.Logger("test", ring)
    log.set_pattern("%l|%v")
    log.set_level(spd.Level.TRACE)
    return log


@pytest.fixture
def unraisable(monkeypatch: pytest.MonkeyPatch) -> list:
    """Collect exceptions reported through sys.unraisablehook."""
    seen: list = []
    monkeypatch.setattr(sys, "unraisablehook", lambda info: seen.append(info.exc_value))
    return seen


def run_python(code: str, timeout: float = 60, leak_warnings: bool = True) -> subprocess.CompletedProcess[str]:
    """Run `code` in a fresh interpreter (for exit-time and console-output behavior).

    nanobind's leak report is enabled, so objects that survive interpreter
    shutdown show up in stderr.
    """
    env = dict(os.environ)
    env.pop("PYSPDLOG_LEAK_WARNINGS", None)
    if leak_warnings:
        env["PYSPDLOG_LEAK_WARNINGS"] = "1"
    return subprocess.run(
        [sys.executable, "-c", textwrap.dedent(code)],
        capture_output=True,
        text=True,
        timeout=timeout,
        check=False,
        env=env,
    )
