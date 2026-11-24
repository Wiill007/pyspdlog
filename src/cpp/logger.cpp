#include "common.h"

#include <utility>

using namespace nb::literals;

namespace pyspdlog {
namespace {

nb::str to_text(nb::handle msg, const nb::args &args, const nb::kwargs &kwargs) {
    if (args.size() == 0 && kwargs.size() == 0) {
        return nb::str(msg);
    }
    return nb::str(nb::str(msg).attr("format")(*args, **kwargs));
}

// The traceback of the exception being handled, or "" outside an except block.
std::string current_traceback() {
    nb::object exc_type = nb::module_::import_("sys").attr("exc_info")()[0];
    if (exc_type.is_none()) {
        return {};
    }
    auto text = nb::cast<std::string>(nb::module_::import_("traceback").attr("format_exc")());
    while (!text.empty() && text.back() == '\n') {
        text.pop_back();
    }
    return text;
}

template <level_enum Level>
void def_level_method(nb::class_<spdlog::logger> &cls, const char *name, const char *signature,
                      const char *doc) {
    cls.def(
        name,
        [](spdlog::logger &logger, nb::handle msg, nb::args args, nb::kwargs kwargs) {
            log_message(logger, Level, msg, args, kwargs);
        },
        nb::arg().none(), nb::arg("args"), nb::arg("kwargs"), nb::sig(signature), doc);
}

std::shared_ptr<spdlog::logger> make_logger(std::string name, std::vector<spdlog::sink_ptr> sinks) {
    return std::make_shared<spdlog::logger>(std::move(name), sinks.begin(), sinks.end());
}

async_logger_ptr new_async_logger(std::string name, std::vector<spdlog::sink_ptr> sinks,
                                  spdlog::async_overflow_policy policy) {
    nb::gil_scoped_release release;
    return make_async_logger(std::move(name), std::move(sinks), policy);
}

}  // namespace

void log_message(spdlog::logger &logger, level_enum level, nb::handle msg, const nb::args &args,
                 const nb::kwargs &kwargs, bool with_traceback) {
    // Same early exit as spdlog::logger::log: skip formatting when the message
    // would be dropped anyway (backtrace keeps messages below the level).
    if (!logger.should_log(level) && !logger.should_backtrace()) {
        return;
    }
    if (!accepts_messages(logger)) {
        return;
    }
    nb::str text = to_text(msg, args, kwargs);
    Py_ssize_t size = 0;
    const char *data = PyUnicode_AsUTF8AndSize(text.ptr(), &size);
    if (!data) {
        throw nb::python_error();
    }
    std::string with_tb;
    if (with_traceback) {
        std::string tb = current_traceback();
        if (!tb.empty()) {
            with_tb.reserve(static_cast<std::size_t>(size) + 1 + tb.size());
            with_tb.append(data, static_cast<std::size_t>(size)).append(1, '\n').append(tb);
            data = with_tb.data();
            size = static_cast<Py_ssize_t>(with_tb.size());
        }
    }
    // `text` stays alive (and so does `data`) until after the call.
    nb::gil_scoped_release release;
    logger.log(level, spdlog::string_view_t(data, static_cast<std::size_t>(size)));
}

void bind_loggers(nb::module_ &m) {
    nb::class_<spdlog::logger> logger(
        m, "Logger",
        "A named logger that sends messages to one or more sinks.\n\n"
        "Messages use str.format() syntax and are only formatted when the level is enabled:\n\n"
        "    logger.info('user {} logged in from {ip}', name, ip=addr)\n\n"
        "Loggers created directly are not registered; see register_logger().");

    logger
        .def(nb::new_(&make_logger), "name"_a, "sinks"_a = std::vector<spdlog::sink_ptr>{})
        .def(nb::new_([](std::string name, spdlog::sink_ptr sink) {
                 return make_logger(std::move(name), {std::move(sink)});
             }),
             "name"_a, "sink"_a)
        .def_prop_ro("name", [](const spdlog::logger &l) { return l.name(); })
        .def_prop_rw(
            "level", [](const spdlog::logger &l) { return l.level(); },
            [](spdlog::logger &l, const LevelArg &level) { l.set_level(level.get()); },
            "Minimum level that is logged (default INFO).")
        .def(
            "set_level", [](spdlog::logger &l, const LevelArg &level) { l.set_level(level.get()); },
            "level"_a)
        .def(
            "should_log",
            [](const spdlog::logger &l, const LevelArg &level) { return l.should_log(level.get()); },
            "level"_a, "Whether a message at `level` would be logged.")
        .def("should_backtrace", &spdlog::logger::should_backtrace,
             "Whether backtrace is enabled (messages below the level are kept for dump_backtrace).")
        .def_prop_ro("flush_level", &spdlog::logger::flush_level,
                     "Messages at or above this level flush the sinks immediately.")
        .def(
            "flush_on", [](spdlog::logger &l, const LevelArg &level) { l.flush_on(level.get()); },
            "level"_a)
        .def("flush", &spdlog::logger::flush, release_gil())
        .def("set_pattern", &spdlog::logger::set_pattern, "pattern"_a,
             "time_type"_a = spdlog::pattern_time_type::local, release_gil(),
             "Set the output format of all of this logger's sinks, e.g. '[%H:%M:%S] [%l] %v'.\n"
             "See https://github.com/gabime/spdlog/wiki/Custom-formatting for the syntax.")
        .def_prop_ro(
            "sinks", [](const spdlog::logger &l) { return l.sinks(); },
            "A copy of the list of sinks.")
        .def("enable_backtrace", &spdlog::logger::enable_backtrace, "n_messages"_a, release_gil(),
             "Keep the last `n_messages` messages of any level so dump_backtrace() can log them.")
        .def("disable_backtrace", &spdlog::logger::disable_backtrace, release_gil())
        .def("dump_backtrace", &spdlog::logger::dump_backtrace, release_gil())
        .def("clone", &clone_logger, "name"_a, release_gil(),
             "Return an unregistered copy that shares the same sinks.")
        .def(
            "log",
            [](spdlog::logger &l, const LevelArg &level, nb::handle msg, nb::args args,
               nb::kwargs kwargs) { log_message(l, level.get(), msg, args, kwargs); },
            nb::arg(), nb::arg().none(), nb::arg("args"), nb::arg("kwargs"),
            nb::sig("def log(self, level: Level | str, msg: object, /, *args: object, "
                    "**kwargs: object) -> None"),
            "Log `msg` at `level`. With arguments, the message is msg.format(*args, **kwargs).")
        .def(
            "exception",
            [](spdlog::logger &l, nb::handle msg, nb::args args, nb::kwargs kwargs) {
                log_message(l, spdlog::level::err, msg, args, kwargs, true);
            },
            nb::arg().none(), nb::arg("args"), nb::arg("kwargs"),
            nb::sig("def exception(self, msg: object, /, *args: object, **kwargs: object) -> None"),
            "Log at ERROR, adding the traceback of the exception being handled.")
        .def("__repr__", [](const spdlog::logger &l) {
            const auto level = spdlog::level::to_string_view(l.level());
            const char *kind = dynamic_cast<const spdlog::async_logger *>(&l) ? "AsyncLogger" : "Logger";
            return nb::str("<{} {!r} level={}>")
                .format(kind, l.name(), std::string_view(level.data(), level.size()));
        });

    def_level_method<spdlog::level::trace>(
        logger, "trace", "def trace(self, msg: object, /, *args: object, **kwargs: object) -> None",
        "Log at TRACE.");
    def_level_method<spdlog::level::debug>(
        logger, "debug", "def debug(self, msg: object, /, *args: object, **kwargs: object) -> None",
        "Log at DEBUG.");
    def_level_method<spdlog::level::info>(
        logger, "info", "def info(self, msg: object, /, *args: object, **kwargs: object) -> None",
        "Log at INFO.");
    def_level_method<spdlog::level::warn>(
        logger, "warn", "def warn(self, msg: object, /, *args: object, **kwargs: object) -> None",
        "Log at WARN.");
    def_level_method<spdlog::level::warn>(
        logger, "warning",
        "def warning(self, msg: object, /, *args: object, **kwargs: object) -> None",
        "Alias of warn().");
    def_level_method<spdlog::level::err>(
        logger, "error", "def error(self, msg: object, /, *args: object, **kwargs: object) -> None",
        "Log at ERROR.");
    def_level_method<spdlog::level::critical>(
        logger, "critical",
        "def critical(self, msg: object, /, *args: object, **kwargs: object) -> None",
        "Log at CRITICAL.");

    nb::class_<spdlog::async_logger, spdlog::logger>(
        m, "AsyncLogger",
        "A logger whose sinks run on the shared background thread pool (see init_thread_pool()). "
        "Logging only formats the message and queues it.")
        .def(nb::new_(&new_async_logger), "name"_a, "sinks"_a = std::vector<spdlog::sink_ptr>{},
             "overflow_policy"_a = spdlog::async_overflow_policy::block)
        .def(nb::new_([](std::string name, spdlog::sink_ptr sink, spdlog::async_overflow_policy policy) {
                 return new_async_logger(std::move(name), {std::move(sink)}, policy);
             }),
             "name"_a, "sink"_a, "overflow_policy"_a = spdlog::async_overflow_policy::block)
        .def("flush", &flush_async_logger, release_gil(),
             "Flush the sinks and wait until every message queued so far has been written.");
}

}  // namespace pyspdlog
