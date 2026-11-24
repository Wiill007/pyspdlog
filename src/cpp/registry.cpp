#include "common.h"

#include <nanobind/stl/chrono.h>
#include <nanobind/stl/filesystem.h>

#include <spdlog/cfg/env.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/daily_file_sink.h>
#include <spdlog/sinks/null_sink.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/stdout_sinks.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <utility>

using namespace nb::literals;

namespace pyspdlog {
namespace {

namespace sinks = spdlog::sinks;
using logger_ptr = std::shared_ptr<spdlog::logger>;

// Creates and registers a logger with one `Sink`, like spdlog's *_mt() factories.
template <typename Sink, typename... Args>
logger_ptr create(const std::string &name, bool asynchronous, Args &&...args) {
    if (asynchronous) {
        auto sink = std::make_shared<Sink>(std::forward<Args>(args)...);
        auto logger = make_async_logger(name, {std::move(sink)}, spdlog::async_overflow_policy::block);
        spdlog::initialize_logger(logger);
        return logger;
    }
    return spdlog::synchronous_factory::create<Sink>(name, std::forward<Args>(args)...);
}

logger_ptr current_default_logger() {
    nb::gil_scoped_release release;
    return spdlog::default_logger();
}

// Module-level logging goes to the default logger, and is a no-op without one
// (after drop_all() or shutdown(), for example).
void log_default(level_enum level, nb::handle msg, const nb::args &args, const nb::kwargs &kwargs,
                 bool with_traceback = false) {
    if (logger_ptr logger = current_default_logger()) {
        log_message(*logger, level, msg, args, kwargs, with_traceback);
    }
}

template <level_enum Level>
void def_level_function(nb::module_ &m, const char *name, const char *signature, const char *doc) {
    m.def(
        name,
        [](nb::handle msg, nb::args args, nb::kwargs kwargs) { log_default(Level, msg, args, kwargs); },
        nb::arg().none(), nb::arg("args"), nb::arg("kwargs"), nb::sig(signature), doc);
}

void flush_every(std::chrono::duration<double> interval) {
    if (interval.count() < 0) {
        throw nb::value_error("flush interval must not be negative");
    }
    set_flush_interval(std::chrono::ceil<std::chrono::milliseconds>(interval));
}

}  // namespace

void bind_registry(nb::module_ &m) {
    // --- Registry -----------------------------------------------------------
    m.def("get", &spdlog::get, "name"_a, release_gil(),
          nb::sig("def get(name: str) -> Logger | None"),
          "Return the registered logger called `name`, or None.");
    m.def("register_logger", &spdlog::register_logger, "logger"_a, release_gil(),
          "Register `logger` under its name. Raises SpdlogError if the name is taken.");
    m.def("register_or_replace", &spdlog::register_or_replace, "logger"_a, release_gil(),
          "Register `logger`, replacing any logger with the same name.");
    m.def("initialize_logger", &spdlog::initialize_logger, "logger"_a, release_gil(),
          "Apply the global settings (pattern, level, flush level...) to `logger` and register "
          "it, like the *_mt() factories do.");
    m.def("drop", &spdlog::drop, "name"_a, release_gil(),
          "Unregister the logger called `name` (no-op if there is none).");
    m.def("drop_all", &spdlog::drop_all, release_gil(),
          "Unregister every logger, including the default logger.");
    m.def(
        "all_loggers",
        [] {
            std::vector<logger_ptr> loggers;
            spdlog::apply_all([&](logger_ptr l) { loggers.push_back(std::move(l)); });
            std::sort(loggers.begin(), loggers.end(),
                      [](const logger_ptr &a, const logger_ptr &b) { return a->name() < b->name(); });
            return loggers;
        },
        release_gil(), "Return every registered logger, sorted by name.");
    m.def("default_logger", &spdlog::default_logger, release_gil(),
          nb::sig("def default_logger() -> Logger | None"),
          "Return the default logger (used by the module-level log functions), or None.");
    m.def("set_default_logger", &spdlog::set_default_logger, nb::arg("logger").none(), release_gil(),
          "Make `logger` the default logger, registering it. None disables module-level logging.");
    m.def("set_automatic_registration", &spdlog::set_automatic_registration, "enabled"_a,
          release_gil(), "Whether the *_mt() factories and initialize_logger() register loggers.");

    // --- Global settings ----------------------------------------------------
    m.def(
        "set_level", [](const LevelArg &level) { spdlog::set_level(level.get()); }, "level"_a,
        release_gil(), "Set the level of every registered logger and of loggers created later.");
    m.def(
        "get_level",
        [] {
            logger_ptr logger = spdlog::default_logger();
            return logger ? logger->level() : spdlog::level::off;
        },
        release_gil(), "Return the default logger's level (OFF without a default logger).");
    m.def(
        "should_log",
        [](const LevelArg &level) {
            logger_ptr logger = spdlog::default_logger();
            return logger && logger->should_log(level.get());
        },
        "level"_a, release_gil(), "Whether the default logger would log a message at `level`.");
    m.def("set_pattern", &spdlog::set_pattern, "pattern"_a,
          "time_type"_a = spdlog::pattern_time_type::local, release_gil(),
          "Set the pattern of every registered logger and of loggers created later.");
    m.def(
        "flush_on", [](const LevelArg &level) { spdlog::flush_on(level.get()); }, "level"_a,
        release_gil(), "Make every registered logger flush on messages at or above `level`.");
    m.def("flush_every", &flush_every, "interval"_a, release_gil());
    m.def(
        "flush_every", [](double seconds) { flush_every(std::chrono::duration<double>(seconds)); },
        "interval"_a, release_gil(),
        "Flush every registered logger periodically from a background thread. `interval` is a "
        "timedelta or a number of seconds; 0 stops the periodic flush.");
    m.def("enable_backtrace", &spdlog::enable_backtrace, "n_messages"_a, release_gil(),
          "Enable backtrace on every registered logger and on loggers created later.");
    m.def("disable_backtrace", &spdlog::disable_backtrace, release_gil());
    m.def(
        "dump_backtrace",
        [] {
            if (logger_ptr logger = spdlog::default_logger()) {
                logger->dump_backtrace();
            }
        },
        release_gil(), "Dump the default logger's backtrace.");
    m.def(
        "load_env_levels", [](const std::string &var) { spdlog::cfg::load_env_levels(var.c_str()); },
        "var"_a = "SPDLOG_LEVEL", release_gil(),
        "Set levels from an environment variable, e.g. SPDLOG_LEVEL='info,mylogger=trace'.");

    // --- Async --------------------------------------------------------------
    m.def("init_thread_pool", &init_thread_pool, "queue_size"_a = spdlog::details::default_async_q_size,
          "thread_count"_a = 1, release_gil(),
          "Create the thread pool used by async loggers, replacing the current one. Call it "
          "before creating async loggers: existing ones keep using the old pool, which is shut "
          "down. Without a call, a pool with one thread and room for 8192 messages is created on "
          "first use.");
    m.def("shutdown", &shutdown_spdlog, release_gil(),
          "Flush and drop every logger and stop the background threads. pyspdlog calls this "
          "automatically at interpreter exit. Async loggers created before it drop their "
          "messages.");
    // Waits for the periodic flusher, which may need the GIL to finish a flush.
    m.def("_before_fork", &before_fork, release_gil());
    m.def("_after_fork_in_parent", &after_fork_in_parent);
    m.def("_after_fork_in_child", &after_fork_in_child);

    // --- Factories ----------------------------------------------------------
    m.def(
        "stdout_color_mt",
        [](const std::string &name, spdlog::color_mode mode, bool asynchronous) {
            return create<sinks::stdout_color_sink_mt>(name, asynchronous, mode);
        },
        "name"_a, "color_mode"_a = spdlog::color_mode::automatic, nb::kw_only(),
        "asynchronous"_a = false, release_gil(),
        "Create a logger that writes colored output to stdout. The logger is registered and "
        "gets the global settings. With asynchronous=True it is an AsyncLogger.");
    m.def(
        "stderr_color_mt",
        [](const std::string &name, spdlog::color_mode mode, bool asynchronous) {
            return create<sinks::stderr_color_sink_mt>(name, asynchronous, mode);
        },
        "name"_a, "color_mode"_a = spdlog::color_mode::automatic, nb::kw_only(),
        "asynchronous"_a = false, release_gil(),
        "Create a logger that writes colored output to stderr (see stdout_color_mt()).");
    m.def(
        "stdout_logger_mt",
        [](const std::string &name, bool asynchronous) {
            return create<sinks::stdout_sink_mt>(name, asynchronous);
        },
        "name"_a, nb::kw_only(), "asynchronous"_a = false, release_gil(),
        "Create a logger that writes to stdout without colors (see stdout_color_mt()).");
    m.def(
        "stderr_logger_mt",
        [](const std::string &name, bool asynchronous) {
            return create<sinks::stderr_sink_mt>(name, asynchronous);
        },
        "name"_a, nb::kw_only(), "asynchronous"_a = false, release_gil(),
        "Create a logger that writes to stderr without colors (see stdout_color_mt()).");
    m.def(
        "basic_logger_mt",
        [](const std::string &name, const std::filesystem::path &filename, bool truncate,
           bool asynchronous) {
            return create<sinks::basic_file_sink_mt>(name, asynchronous, filename.string(), truncate);
        },
        "name"_a, "filename"_a, "truncate"_a = false, nb::kw_only(), "asynchronous"_a = false,
        release_gil(), "Create a logger that writes to a file (see BasicFileSink, stdout_color_mt()).");
    m.def(
        "rotating_logger_mt",
        [](const std::string &name, const std::filesystem::path &filename, std::size_t max_size,
           std::size_t max_files, bool rotate_on_open, bool asynchronous) {
            return create<sinks::rotating_file_sink_mt>(name, asynchronous, filename.string(), max_size,
                                                        max_files, rotate_on_open);
        },
        "name"_a, "filename"_a, "max_size"_a, "max_files"_a, "rotate_on_open"_a = false,
        nb::kw_only(), "asynchronous"_a = false, release_gil(),
        "Create a logger that writes to a size-rotated file (see RotatingFileSink, "
        "stdout_color_mt()).");
    m.def(
        "daily_logger_mt",
        [](const std::string &name, const std::filesystem::path &filename, int hour, int minute,
           bool truncate, std::uint16_t max_files, bool asynchronous) {
            return create<sinks::daily_file_sink_mt>(name, asynchronous, filename.string(), hour, minute,
                                                     truncate, max_files);
        },
        "name"_a, "filename"_a, "hour"_a = 0, "minute"_a = 0, "truncate"_a = false, "max_files"_a = 0,
        nb::kw_only(), "asynchronous"_a = false, release_gil(),
        "Create a logger that writes to a new file every day (see DailyFileSink, "
        "stdout_color_mt()).");
    m.def(
        "null_logger_mt",
        [](const std::string &name) { return create<sinks::null_sink_mt>(name, false); }, "name"_a,
        release_gil(), "Create a registered logger that discards everything.");

    // --- Logging through the default logger ---------------------------------
    m.def(
        "log",
        [](const LevelArg &level, nb::handle msg, nb::args args, nb::kwargs kwargs) {
            log_default(level.get(), msg, args, kwargs);
        },
        nb::arg(), nb::arg().none(), nb::arg("args"), nb::arg("kwargs"),
        nb::sig("def log(level: Level | str, msg: object, /, *args: object, **kwargs: object) -> None"),
        "Log through the default logger (see Logger.log()).");
    m.def(
        "exception",
        [](nb::handle msg, nb::args args, nb::kwargs kwargs) {
            log_default(spdlog::level::err, msg, args, kwargs, true);
        },
        nb::arg().none(), nb::arg("args"), nb::arg("kwargs"),
        nb::sig("def exception(msg: object, /, *args: object, **kwargs: object) -> None"),
        "Log at ERROR through the default logger, adding the current exception's traceback.");
    def_level_function<spdlog::level::trace>(
        m, "trace", "def trace(msg: object, /, *args: object, **kwargs: object) -> None",
        "Log at TRACE through the default logger.");
    def_level_function<spdlog::level::debug>(
        m, "debug", "def debug(msg: object, /, *args: object, **kwargs: object) -> None",
        "Log at DEBUG through the default logger.");
    def_level_function<spdlog::level::info>(
        m, "info", "def info(msg: object, /, *args: object, **kwargs: object) -> None",
        "Log at INFO through the default logger.");
    def_level_function<spdlog::level::warn>(
        m, "warn", "def warn(msg: object, /, *args: object, **kwargs: object) -> None",
        "Log at WARN through the default logger.");
    def_level_function<spdlog::level::warn>(
        m, "warning", "def warning(msg: object, /, *args: object, **kwargs: object) -> None",
        "Alias of warn().");
    def_level_function<spdlog::level::err>(
        m, "error", "def error(msg: object, /, *args: object, **kwargs: object) -> None",
        "Log at ERROR through the default logger.");
    def_level_function<spdlog::level::critical>(
        m, "critical", "def critical(msg: object, /, *args: object, **kwargs: object) -> None",
        "Log at CRITICAL through the default logger.");
}

}  // namespace pyspdlog
