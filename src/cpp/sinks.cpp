#include "common.h"

#include <nanobind/stl/chrono.h>
#include <nanobind/stl/filesystem.h>
#include <nanobind/stl/optional.h>

#include <spdlog/pattern_formatter.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/daily_file_sink.h>
#include <spdlog/sinks/null_sink.h>
#include <spdlog/sinks/ringbuffer_sink.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/stdout_sinks.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <unordered_set>
#include <utility>

using namespace nb::literals;

namespace pyspdlog {
namespace {

namespace sinks = spdlog::sinks;
using spdlog::details::log_msg;

struct LogMessage {
    std::string logger_name;
    level_enum level;
    std::string payload;
    std::string formatted;
    std::chrono::system_clock::time_point time;
    std::size_t thread_id;
};

std::string_view strip_eol(std::string_view text) {
    constexpr std::string_view eol = SPDLOG_EOL;
    if (text.size() >= eol.size() && text.substr(text.size() - eol.size()) == eol) {
        text.remove_suffix(eol.size());
    }
    return text;
}

// Drops a Python reference from any thread. Once the interpreter is gone the
// reference is leaked on purpose: touching it then would crash the process.
void release_python_object(nb::object &obj) noexcept {
    if (!obj.is_valid()) {
        return;
    }
    if (nb::detail::cleanup_guard guard{}) {
        obj = nb::object();
    } else {
        (void) obj.release();
    }
}

class python_sink;

// Every live python_sink, so their Python references can be dropped at exit.
// Heap-allocated and never freed: sinks may be destroyed during static
// destruction, after a static set would already be gone.
std::mutex &live_sinks_mutex() {
    static auto *mutex = new std::mutex();
    return *mutex;
}

std::unordered_set<python_sink *> &live_sinks() {
    static auto *sinks = new std::unordered_set<python_sink *>();
    return *sinks;
}

// Base class for sinks implemented in terms of Python objects.
//
// spdlog's base_sink holds its mutex for the whole write. This class only
// holds it while formatting, so the Python call runs without it: a callback
// can log again (even to the same logger) and no thread ever waits for the
// GIL while holding a sink lock.
//
// Subclasses keep their Python objects in nb::object members, read them only
// with the GIL held, and treat an empty member as "released" (see
// release_python_sinks()).
class python_sink : public sinks::sink {
public:
    explicit python_sink(const char *context)
        : context_(context), formatter_(std::make_unique<spdlog::pattern_formatter>()) {
        std::lock_guard<std::mutex> lock(live_sinks_mutex());
        live_sinks().insert(this);
    }

    ~python_sink() override {
        std::lock_guard<std::mutex> lock(live_sinks_mutex());
        live_sinks().erase(this);
    }

    // Moves this sink's Python references into `out`. Requires the GIL.
    virtual void take_python_refs(std::vector<nb::object> &out) = 0;

    void log(const log_msg &msg) final {
        spdlog::memory_buf_t formatted;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            formatter_->format(msg, formatted);
        }
        nb::detail::cleanup_guard guard;
        if (!guard) {
            return;
        }
        try {
            emit(msg, std::string_view(formatted.data(), formatted.size()));
        } catch (...) {
            report_current_exception();
        }
    }

    void flush() final {
        if (!wants_flush()) {
            return;
        }
        nb::detail::cleanup_guard guard;
        if (!guard) {
            return;
        }
        try {
            do_flush();
        } catch (...) {
            report_current_exception();
        }
    }

    void set_pattern(const std::string &pattern) final {
        set_formatter(std::make_unique<spdlog::pattern_formatter>(pattern));
    }

    void set_formatter(std::unique_ptr<spdlog::formatter> formatter) final {
        std::lock_guard<std::mutex> lock(mutex_);
        formatter_ = std::move(formatter);
    }

protected:
    // Called with the GIL held.
    virtual void emit(const log_msg &msg, std::string_view formatted) = 0;
    virtual bool wants_flush() const { return false; }
    virtual void do_flush() {}

private:
    // Sinks may run on a worker thread with nobody to raise to, so Python
    // errors are reported through sys.unraisablehook, like errors in __del__.
    void report_current_exception() noexcept {
        try {
            throw;
        } catch (nb::python_error &e) {
            e.discard_as_unraisable(context_);
        } catch (const std::exception &e) {
            PyErr_SetString(PyExc_RuntimeError, e.what());
            nb::python_error error;
            error.discard_as_unraisable(context_);
        } catch (...) {
        }
    }

    const char *context_;
    std::mutex mutex_;
    std::unique_ptr<spdlog::formatter> formatter_;
};

class callback_sink final : public python_sink {
public:
    callback_sink(nb::callable callback, std::optional<nb::callable> flush)
        : python_sink("pyspdlog.CallbackSink callback"), callback_(std::move(callback)) {
        if (flush) {
            flush_ = std::move(*flush);
            has_flush_ = true;
        }
    }

    ~callback_sink() override {
        release_python_object(callback_);
        release_python_object(flush_);
    }

    nb::object callback() const { return callback_.is_valid() ? callback_ : nb::none(); }

    void take_python_refs(std::vector<nb::object> &out) override {
        out.push_back(std::move(callback_));
        out.push_back(std::move(flush_));
    }

protected:
    void emit(const log_msg &msg, std::string_view formatted) override {
        // A new reference, so the callable stays alive even if the sink is
        // released while the call is running.
        nb::object callback = callback_;
        if (!callback.is_valid()) {
            return;
        }
        LogMessage record{std::string(msg.logger_name.data(), msg.logger_name.size()),
                          msg.level,
                          std::string(msg.payload.data(), msg.payload.size()),
                          std::string(strip_eol(formatted)),
                          msg.time,
                          msg.thread_id};
        callback(nb::cast(std::move(record)));
    }

    bool wants_flush() const override { return has_flush_; }

    void do_flush() override {
        nb::object flush = flush_;
        if (flush.is_valid()) {
            flush();
        }
    }

private:
    nb::object callback_;
    nb::object flush_;
    bool has_flush_ = false;  // lets flush() skip taking the GIL
};

class stream_sink final : public python_sink {
public:
    explicit stream_sink(nb::object stream)
        : python_sink("pyspdlog.StreamSink write"), stream_(std::move(stream)) {}

    ~stream_sink() override { release_python_object(stream_); }

    nb::object stream() const { return stream_.is_valid() ? stream_ : nb::none(); }

    void take_python_refs(std::vector<nb::object> &out) override {
        out.push_back(std::move(stream_));
    }

protected:
    void emit(const log_msg &, std::string_view formatted) override {
        nb::object target = resolve();
        if (target.is_none()) {
            return;
        }
        // Python text streams translate "\n" themselves, so always end with "\n".
        std::string line(strip_eol(formatted));
        line += '\n';
        nb::object text = nb::steal(
            PyUnicode_DecodeUTF8(line.data(), static_cast<Py_ssize_t>(line.size()), "replace"));
        if (!text.is_valid()) {
            throw nb::python_error();
        }
        target.attr("write")(text);
    }

    bool wants_flush() const override { return true; }

    void do_flush() override {
        nb::object target = resolve();
        if (!target.is_none()) {
            target.attr("flush")();
        }
    }

private:
    // None means "whatever sys.stdout is right now", which keeps output
    // visible under pytest's capture, Jupyter, and redirect_stdout().
    nb::object resolve() const {
        if (!stream_.is_valid()) {
            return nb::none();  // released
        }
        if (!stream_.is_none()) {
            return stream_;
        }
        PyObject *out = PySys_GetObject("stdout");  // borrowed, may be NULL
        return nb::borrow(out ? out : Py_None);
    }

    nb::object stream_;
};

template <typename Sink, typename... Args>
auto make_sink(Args &&...args) {
    return std::make_shared<Sink>(std::forward<Args>(args)...);
}

}  // namespace

void release_python_sinks() {
    std::vector<nb::object> refs;
    {
        std::lock_guard<std::mutex> lock(live_sinks_mutex());
        for (python_sink *sink : live_sinks()) {
            sink->take_python_refs(refs);
        }
    }
    // `refs` is destroyed here, outside the lock: dropping the references can
    // destroy loggers and with them other python_sinks.
}

void bind_sinks(nb::module_ &m) {
    nb::class_<LogMessage>(m, "LogMessage", "A log record, as passed to CallbackSink callbacks.")
        .def_ro("logger_name", &LogMessage::logger_name, "Name of the logger that emitted it.")
        .def_ro("level", &LogMessage::level)
        .def_ro("payload", &LogMessage::payload, "The message text, without any pattern applied.")
        .def_ro("formatted", &LogMessage::formatted,
                "The message rendered with the sink's pattern, without a trailing newline.")
        .def_ro("time", &LogMessage::time, "When it was logged, as a naive local datetime.")
        .def_ro("thread_id", &LogMessage::thread_id, "OS id of the thread that logged it.")
        .def("__repr__", [](const LogMessage &msg) {
            const auto level = spdlog::level::to_string_view(msg.level);
            return nb::str("LogMessage(logger_name={!r}, level={}, payload={!r})")
                .format(msg.logger_name, std::string_view(level.data(), level.size()), msg.payload);
        });

    nb::class_<sinks::sink>(m, "Sink",
                            "Base class of all sinks. A sink writes log messages somewhere; one sink "
                            "can be shared by several loggers.")
        .def_prop_rw(
            "level", [](const sinks::sink &s) { return s.level(); },
            [](sinks::sink &s, const LevelArg &level) { s.set_level(level.get()); },
            "Minimum level this sink writes (default TRACE: everything the logger passes on).")
        .def(
            "set_level", [](sinks::sink &s, const LevelArg &level) { s.set_level(level.get()); },
            "level"_a)
        .def(
            "should_log",
            [](const sinks::sink &s, const LevelArg &level) { return s.should_log(level.get()); },
            "level"_a)
        .def("set_pattern", &sinks::sink::set_pattern, "pattern"_a, release_gil(),
             "Set the format of this sink's output (spdlog pattern syntax, e.g. '%H:%M:%S %v').")
        .def("flush", &sinks::sink::flush, release_gil());

    nb::class_<sinks::stdout_sink_mt, sinks::sink>(m, "StdoutSink", "Writes to the process stdout.")
        .def(nb::new_([] { return make_sink<sinks::stdout_sink_mt>(); }));

    nb::class_<sinks::stderr_sink_mt, sinks::sink>(m, "StderrSink", "Writes to the process stderr.")
        .def(nb::new_([] { return make_sink<sinks::stderr_sink_mt>(); }));

    nb::class_<sinks::stdout_color_sink_mt, sinks::sink>(m, "StdoutColorSink",
                                                         "Writes colored output to the process stdout.")
        .def(nb::new_([](spdlog::color_mode mode) { return make_sink<sinks::stdout_color_sink_mt>(mode); }),
             "color_mode"_a = spdlog::color_mode::automatic);

    nb::class_<sinks::stderr_color_sink_mt, sinks::sink>(m, "StderrColorSink",
                                                         "Writes colored output to the process stderr.")
        .def(nb::new_([](spdlog::color_mode mode) { return make_sink<sinks::stderr_color_sink_mt>(mode); }),
             "color_mode"_a = spdlog::color_mode::automatic);

    nb::class_<sinks::basic_file_sink_mt, sinks::sink>(m, "BasicFileSink", "Writes to a single file.")
        .def(nb::new_([](const std::filesystem::path &filename, bool truncate) {
                 return make_sink<sinks::basic_file_sink_mt>(filename.string(), truncate);
             }),
             "filename"_a, "truncate"_a = false,
             "Open (and create, including parent directories) `filename`. With `truncate`, "
             "existing contents are discarded; otherwise new messages are appended.")
        .def_prop_ro("filename",
                     [](sinks::basic_file_sink_mt &s) {
                         nb::gil_scoped_release release;
                         return s.filename();
                     })
        .def("truncate", &sinks::basic_file_sink_mt::truncate, release_gil(),
             "Discard the file's current contents.");

    nb::class_<sinks::rotating_file_sink_mt, sinks::sink>(
        m, "RotatingFileSink",
        "Writes to a file and rotates it when it reaches `max_size` bytes, keeping "
        "`max_files` old files (log.txt -> log.1.txt -> log.2.txt ...).")
        .def(nb::new_([](const std::filesystem::path &filename, std::size_t max_size,
                         std::size_t max_files, bool rotate_on_open) {
                 return make_sink<sinks::rotating_file_sink_mt>(filename.string(), max_size, max_files,
                                                                rotate_on_open);
             }),
             "filename"_a, "max_size"_a, "max_files"_a, "rotate_on_open"_a = false)
        .def_prop_ro("filename",
                     [](sinks::rotating_file_sink_mt &s) {
                         nb::gil_scoped_release release;
                         return s.filename();
                     })
        .def("rotate_now", &sinks::rotating_file_sink_mt::rotate_now, release_gil());

    nb::class_<sinks::daily_file_sink_mt, sinks::sink>(
        m, "DailyFileSink",
        "Writes to a new file every day at rotation_hour:rotation_minute. The date is added to "
        "the file name (log.txt -> log_2026-01-31.txt). With `max_files` > 0, only that many "
        "files are kept.")
        .def(nb::new_([](const std::filesystem::path &filename, int rotation_hour, int rotation_minute,
                         bool truncate, std::uint16_t max_files) {
                 return make_sink<sinks::daily_file_sink_mt>(filename.string(), rotation_hour,
                                                             rotation_minute, truncate, max_files);
             }),
             "filename"_a, "rotation_hour"_a = 0, "rotation_minute"_a = 0, "truncate"_a = false,
             "max_files"_a = 0)
        .def_prop_ro("filename", [](sinks::daily_file_sink_mt &s) {
            nb::gil_scoped_release release;
            return s.filename();
        });

    nb::class_<sinks::null_sink_mt, sinks::sink>(m, "NullSink", "Discards everything.")
        .def(nb::new_([] { return make_sink<sinks::null_sink_mt>(); }));

    nb::class_<sinks::ringbuffer_sink_mt, sinks::sink>(
        m, "RingbufferSink", "Keeps the last `n_items` messages in memory (handy for tests).")
        .def(nb::new_([](std::size_t n_items) { return make_sink<sinks::ringbuffer_sink_mt>(n_items); }),
             "n_items"_a)
        .def(
            "last_formatted",
            [](sinks::ringbuffer_sink_mt &s, std::size_t limit) {
                auto lines = s.last_formatted(limit);
                for (auto &line : lines) {
                    line.resize(strip_eol(line).size());
                }
                return lines;
            },
            "limit"_a = 0, release_gil(),
            "Return the stored messages, oldest first, formatted and without trailing newlines. "
            "With `limit` > 0, return at most that many of the newest ones.");

    nb::class_<callback_sink, sinks::sink>(
        m, "CallbackSink",
        "Calls `callback(message: LogMessage)` for every message. Exceptions raised by the "
        "callback are reported through sys.unraisablehook. With an async logger the callback "
        "runs on a worker thread.")
        .def(nb::new_([](nb::callable callback, std::optional<nb::callable> flush) {
                 return make_sink<callback_sink>(std::move(callback), std::move(flush));
             }),
             "callback"_a, "flush"_a = nb::none(),
             "`flush`, if given, is called with no arguments when the sink is flushed.")
        .def_prop_ro("callback", &callback_sink::callback);

    nb::class_<stream_sink, sinks::sink>(
        m, "StreamSink",
        "Writes formatted lines to a Python text stream such as sys.stderr or an open file. "
        "With stream=None it writes to whatever sys.stdout is at the time, so output shows up "
        "in Jupyter notebooks and under pytest capture.")
        .def(nb::new_([](nb::object stream) { return make_sink<stream_sink>(std::move(stream)); }),
             "stream"_a = nb::none())
        .def_prop_ro("stream", &stream_sink::stream);
}

}  // namespace pyspdlog
