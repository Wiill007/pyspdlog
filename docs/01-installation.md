# 1. Installation

## Requirements

- Linux or macOS. Windows is not supported.
- CPython 3.10 or newer.
- To build from source:
  - A C++17 compiler (GCC 9+ or Clang 10+; Xcode command line tools on macOS).
  - Internet access during the build, to download spdlog. See
    [Using an installed spdlog](#using-an-installed-spdlog) to avoid that.

  CMake and Ninja are downloaded automatically when they are missing, because the
  build backend (scikit-build-core) requests them from PyPI.

pyspdlog has no runtime dependencies. spdlog and fmt are compiled into the extension.

## Installing

pyspdlog is not published on PyPI yet, so install it from GitHub. This builds the
extension, which takes about a minute:

```sh
pip install git+https://github.com/Wiill007/spdlog-python-bindings
# or, in a uv project
uv add git+https://github.com/Wiill007/spdlog-python-bindings
```

Add `@<branch>` or `@<commit>` to the URL to pin a version.

From a clone of the repository:

```sh
pip install .
# or, for development (see 18-development.md)
uv sync
```

### Prebuilt wheels

The GitLab CI/CD pipeline builds Linux wheels on the default branch, on tags and in
pipelines started by hand. Download them from the pipeline's job artifacts:

| Job | Artifact contents |
| --- | --- |
| `wheels:linux-x86_64` | Linux x86_64, glibc (manylinux) and musl (musllinux) |
| `wheels:linux-aarch64` | Linux aarch64, glibc and musl (a manual job: start it from the pipeline page) |

Each job produces one wheel per Python version for 3.10 and 3.11, and one
`cp312-abi3` wheel that works on Python 3.12 and every later version:

```sh
pip install pyspdlog-0.1.0-cp312-abi3-manylinux_2_27_x86_64.manylinux_2_28_x86_64.whl
```

There are no prebuilt macOS wheels. Install from source there, or build wheels on a Mac
with cibuildwheel ([18. Development](18-development.md#building-distributions)).

## Checking the installation

```sh
python -c "import pyspdlog; print(pyspdlog.__version__, pyspdlog.SPDLOG_VERSION)"
# 0.1.0 1.17.0
```

## Build options

Build options are CMake variables. With pip, pass them as config settings:

```sh
pip install . -C cmake.define.PYSPDLOG_USE_SYSTEM_SPDLOG=ON
```

| Option | Default | Effect |
| --- | --- | --- |
| `PYSPDLOG_USE_SYSTEM_SPDLOG` | `OFF` | Use an installed spdlog instead of downloading it. |
| `PYSPDLOG_STUBS` | `ON` | Generate the `_core.pyi` type stub. This imports the freshly built module, so turn it off when cross-compiling. |

The build type is `Release` unless you set `CMAKE_BUILD_TYPE` (for example
`-C cmake.build-type=Debug`).

### Using an installed spdlog

With `PYSPDLOG_USE_SYSTEM_SPDLOG=ON`, CMake looks for spdlog 1.x (1.12 or newer)
with `find_package(spdlog)`. Install the development package first:

| System | Package |
| --- | --- |
| Fedora | `spdlog-devel` |
| Debian, Ubuntu | `libspdlog-dev` |
| Arch | `spdlog` |
| Homebrew | `spdlog` |

The extension then links to the system's `libspdlog` and `libfmt`. That is
convenient for local development, but the result only works on machines that have
the same libraries, so do not distribute wheels built this way.

---

[Index](README.md) · Next: [2. Quick start](02-quickstart.md)
