# 18. Development

## Setup

You need [uv](https://docs.astral.sh/uv/), a C++17 compiler and internet access for the
first build (to download spdlog).

```sh
git clone <repository> && cd spdlog-python-bindings
uv sync                 # builds the extension and installs it, editable, into .venv
uv run pytest           # about 2 seconds
```

`uv sync` / `uv run` rebuild the extension when `pyproject.toml`, `CMakeLists.txt` or
anything under `src/cpp/` changes (`[tool.uv] cache-keys`). Python files under
`src/pyspdlog/` are used directly, with no rebuild.

### Faster C++ iteration

For a tight edit-compile loop, build with CMake directly and import the module from the
build directory:

```sh
uv pip install nanobind
cmake -S . -B build/dev -G Ninja -DPython_EXECUTABLE=.venv/bin/python
cmake --build build/dev
PYTHONPATH=build/dev .venv/bin/python -c "import _core; _core.info('hi')"
```

This imports `_core` on its own, without the package's `atexit` and fork hooks.

## Layout

```text
src/cpp/          the extension (see 17-design-notes.md)
src/pyspdlog/     the Python package
tests/            pytest suite
benchmarks/       bench.py: comparison with the logging module
docs/             this documentation
CMakeLists.txt    build: spdlog download, nanobind module, stubs
pyproject.toml    packaging, pytest, uv and cibuildwheel settings
.gitlab-ci.yml    CI/CD pipeline
```

## Tests

```sh
uv run pytest                          # everything
uv run pytest tests/test_async.py      # one file
uv run pytest -k fork -v               # by name
```

| File | Covers |
| --- | --- |
| `test_levels.py` | `Level`, level names, filtering |
| `test_logger.py` | formatting, methods, backtrace, threads |
| `test_sinks.py` | every sink, Python sinks, console output |
| `test_registry.py` | registry, factories, global settings |
| `test_async.py` | async loggers, flush barrier, overflow, deadlock stress |
| `test_handler.py` | `SpdlogHandler` |
| `test_lifecycle.py` | exit, fork, reference cycles, background-thread guards |
| `test_package.py` | exports, versions, typing files |

Conventions:

- An autouse fixture in `conftest.py` resets the registry and global settings after each
  test.
- **Exit-time behavior can only be tested in a fresh interpreter.** Use `run_python(code)`
  from `conftest.py`. It runs the code in a subprocess with `PYSPDLOG_LEAK_WARNINGS=1`,
  so leaks show up in `stderr`.
- Warnings are errors (`filterwarnings = ["error"]`). An exception in a Python sink
  therefore fails the test, unless it uses the `unraisable` fixture to capture it.
- Every test has a 120 s timeout (`pytest-timeout`), so a deadlock fails instead of
  hanging.

Threading bugs are often intermittent. After touching `threads.cpp` or the sinks, run
the suite repeatedly:

```sh
for i in $(seq 30); do PYSPDLOG_LEAK_WARNINGS=1 uv run pytest -q -x || break; done
```

## Benchmarks

```sh
uv run python benchmarks/bench.py
```

## Building distributions

```sh
uv build                 # sdist and a wheel for the current interpreter into dist/
uv build --wheel --python 3.11
```

Portable Linux wheels (manylinux and musllinux) are built with
[cibuildwheel](https://cibuildwheel.pypa.io/), configured in `pyproject.toml`
(`[tool.cibuildwheel]`). It builds CPython 3.10 and 3.11 wheels plus one `cp312-abi3`
wheel, and runs the test suite against each one. Locally, with Docker or Podman:

```sh
uvx cibuildwheel@3.1.4 --platform linux                          # Docker
CIBW_CONTAINER_ENGINE=podman uvx cibuildwheel@3.1.4 --platform linux
CIBW_BUILD="cp312-manylinux_x86_64" uvx cibuildwheel@3.1.4 --platform linux  # just one
```

On macOS, `uvx cibuildwheel --platform macos` builds macOS wheels.

## CI/CD (GitLab)

`.gitlab-ci.yml` defines:

| Job | Stage | Runs | Does |
| --- | --- | --- | --- |
| `test: [3.10]`, `[3.11]`, `[3.13]` | test | every push and merge request | `uv sync --locked` + pytest on `python:<version>` images; JUnit report shown in the MR |
| `wheels:linux-x86_64` | build | default branch, tags, manual pipelines | cibuildwheel (Docker-in-Docker): manylinux + musllinux wheels, each tested |
| `wheels:linux-aarch64` | build | started by hand from those pipelines | the same for aarch64, under QEMU emulation (slow) |
| `sdist` | build | default branch, tags, manual pipelines | `uv build --sdist` |

Wheels and the sdist are kept as job artifacts (`wheelhouse/`, `dist/`). Download them
from the pipeline page.

Notes:

- The wheel jobs need a runner that allows privileged Docker-in-Docker services, as
  GitLab.com's hosted Linux runners do. On self-managed runners, enable `privileged`
  in the runner configuration.
- `uv sync --locked` fails if `uv.lock` is out of date. Run `uv lock` and commit the
  result after changing dependencies.
- macOS is not in CI (GitLab's macOS runners need a paid tier).

## Changing the spdlog version

1. Update the URL in `CMakeLists.txt` (`FetchContent_Declare(spdlog ...)`).
2. Update `URL_HASH`:
   ```sh
   curl -sL https://github.com/gabime/spdlog/archive/refs/tags/vX.Y.Z.tar.gz | sha256sum
   ```
3. Rebuild and run the tests. spdlog's internals used by pyspdlog (the registry's thread
   pool accessors, `async_logger`'s constructors) can change between versions.

## nanobind version

`pyproject.toml` requires `nanobind>=3.1,<4`. The bindings use
`nb::detail::cleanup_guard`, which is not part of nanobind's public API, so check it
still exists when moving to a new major version.

## Keeping the docs current

The API reference ([16](16-api-reference.md)) is written by hand. After changing the
bindings, compare it with the generated stub:

```sh
.venv/bin/python -c "import pyspdlog, pathlib; print(pathlib.Path(pyspdlog._core.__file__).with_name('_core.pyi').read_text())"
```

---

[17. Design notes](17-design-notes.md) · [Index](README.md)
