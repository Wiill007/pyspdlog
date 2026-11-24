from __future__ import annotations

import re
from importlib import resources

import pyspdlog as spd


def test_versions():
    assert re.fullmatch(r"\d+\.\d+\.\d+.*", spd.__version__)
    assert re.fullmatch(r"\d+\.\d+\.\d+", spd.SPDLOG_VERSION)


def test_all_names_exist():
    missing = [name for name in spd.__all__ if not hasattr(spd, name)]
    assert missing == []
    assert len(set(spd.__all__)) == len(spd.__all__)


def test_typing_files_are_installed():
    package = resources.files("pyspdlog")
    assert package.joinpath("py.typed").is_file()
    stub = package.joinpath("_core.pyi")
    assert stub.is_file()
    text = stub.read_text()
    assert "class Logger" in text
    assert "def get(name: str) -> Logger | None" in text


def test_sink_classes_share_the_base_class():
    for cls in (
        spd.StdoutSink,
        spd.StderrSink,
        spd.StdoutColorSink,
        spd.StderrColorSink,
        spd.BasicFileSink,
        spd.RotatingFileSink,
        spd.DailyFileSink,
        spd.NullSink,
        spd.RingbufferSink,
        spd.CallbackSink,
        spd.StreamSink,
    ):
        assert issubclass(cls, spd.Sink)
    assert issubclass(spd.AsyncLogger, spd.Logger)


def test_sink_base_class_cannot_be_instantiated():
    import pytest

    with pytest.raises(TypeError):
        spd.Sink()
