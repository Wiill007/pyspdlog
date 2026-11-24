#pragma once

// Threading rules for the whole extension
// ---------------------------------------
// spdlog holds its own mutexes (registry, thread pool queue, backtracer, sinks)
// while it runs sinks, and the Python-backed sinks in sinks.cpp need the GIL.
// To make that deadlock-free:
//
//  1. Never wait on a spdlog mutex while holding the GIL. Every binding that
//     can take a spdlog lock releases the GIL first, either with the
//     `release_gil` call guard or an explicit nb::gil_scoped_release.
//  2. Python-backed sinks never hold their own mutex while calling Python.
//  3. Code that touches Python from a spdlog thread (async workers, the
//     periodic flusher) enters the interpreter through
//     nb::detail::cleanup_guard, which also skips the work once the
//     interpreter is shutting down.

#include <nanobind/nanobind.h>
#include <nanobind/stl/shared_ptr.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/string_view.h>
#include <nanobind/stl/vector.h>

#include <spdlog/async.h>
#include <spdlog/sinks/sink.h>
#include <spdlog/spdlog.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace nb = nanobind;

namespace pyspdlog {

using level_enum = spdlog::level::level_enum;
using release_gil = nb::call_guard<nb::gil_scoped_release>;

// A level argument that accepts either a `Level` member or a level name such
// as "info". Unknown names are only rejected by get(), so the error is a
// ValueError rather than a generic "incompatible arguments" TypeError.
struct LevelArg {
    level_enum level = spdlog::level::off;
    std::string name;
    bool by_name = false;

    level_enum get() const;
};

// Case-insensitive level name lookup; raises ValueError for unknown names.
level_enum parse_level(std::string_view name);

// Formats `msg` with str.format(*args, **kwargs) (only when the level is
// enabled and arguments were given) and logs the result. Requires the GIL.
void log_message(spdlog::logger &logger, level_enum level, nb::handle msg, const nb::args &args,
                 const nb::kwargs &kwargs, bool with_traceback = false);

// --- Background threads (threads.cpp) ---------------------------------------
// Unless noted otherwise these take spdlog locks and must be called without
// the GIL.

using async_logger_ptr = std::shared_ptr<spdlog::async_logger>;

// Replaces the shared thread pool.
void init_thread_pool(std::size_t queue_size, std::size_t thread_count);
// Creates an async logger on the shared thread pool (creating a default pool
// if needed). Async loggers remember which pool they were created with, so
// they can tell when its threads are gone (after shutdown(), or in a child
// process after os.fork()) and drop messages instead of queueing them forever.
async_logger_ptr make_async_logger(std::string name, std::vector<spdlog::sink_ptr> sinks,
                                   spdlog::async_overflow_policy policy);
// logger.clone(), keeping track of the pool for async loggers.
std::shared_ptr<spdlog::logger> clone_logger(spdlog::logger &logger, std::string name);
// Flushes and waits until every message queued so far has been processed.
void flush_async_logger(spdlog::async_logger &logger);
// Whether `logger` can still process messages (false only for an async logger
// whose pool is gone). The GIL version warns once if that is due to a fork.
bool accepts_messages(spdlog::logger &logger);  // requires the GIL
bool accepts_messages_nogil(spdlog::logger &logger);
// Starts, replaces or (with 0) stops the periodic flush of registered loggers.
void set_flush_interval(std::chrono::milliseconds interval);
// Stops the background threads and drops every logger.
void shutdown_spdlog();
// os.register_at_fork() handlers.
void before_fork();
void after_fork_in_parent();
void after_fork_in_child();

// Drops the Python references held by CallbackSink/StreamSink instances, which
// then do nothing. Called at exit, after shutdown(): those references can form
// cycles through C++ (callback -> module globals -> logger -> sink -> callback)
// that Python's garbage collector cannot see. Requires the GIL.
void release_python_sinks();

void bind_sinks(nb::module_ &m);
void bind_loggers(nb::module_ &m);
void bind_registry(nb::module_ &m);

}  // namespace pyspdlog

NAMESPACE_BEGIN(NB_NAMESPACE)
NAMESPACE_BEGIN(detail)

template <>
struct type_caster<pyspdlog::LevelArg> {
    NB_TYPE_CASTER(pyspdlog::LevelArg,
                   make_caster<spdlog::level::level_enum>::Name + const_name(" | str"))

    bool from_python(handle src, uint8_t flags, cleanup_list *) noexcept {
        const bool convert = (flags & static_cast<uint8_t>(cast_flags::convert)) != 0;
        if (::nanobind::try_cast<spdlog::level::level_enum>(src, value.level, convert)) {
            value.by_name = false;
            return true;
        }
        if (PyUnicode_Check(src.ptr())) {
            Py_ssize_t size = 0;
            const char *data = PyUnicode_AsUTF8AndSize(src.ptr(), &size);
            if (!data) {
                PyErr_Clear();
                return false;
            }
            value.name.assign(data, static_cast<std::size_t>(size));
            value.by_name = true;
            return true;
        }
        return false;
    }

    static handle from_cpp(const pyspdlog::LevelArg &value, rv_policy policy,
                           cleanup_list *cleanup) noexcept {
        return make_caster<spdlog::level::level_enum>::from_cpp(value.level, policy, cleanup);
    }
};

NAMESPACE_END(detail)
NAMESPACE_END(NB_NAMESPACE)
