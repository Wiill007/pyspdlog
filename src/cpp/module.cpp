#include "common.h"

#include <spdlog/version.h>

#include <cctype>
#include <cstdlib>
#include <utility>

using namespace nb::literals;

namespace pyspdlog {

level_enum parse_level(std::string_view name) {
    std::string lower(name);
    for (char &c : lower) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }

    static const std::pair<std::string_view, level_enum> names[] = {
        {"trace", spdlog::level::trace}, {"debug", spdlog::level::debug},
        {"info", spdlog::level::info},   {"warn", spdlog::level::warn},
        {"warning", spdlog::level::warn}, {"err", spdlog::level::err},
        {"error", spdlog::level::err},   {"critical", spdlog::level::critical},
        {"off", spdlog::level::off},
    };
    for (const auto &[level_name, level] : names) {
        if (lower == level_name) {
            return level;
        }
    }
    throw nb::value_error(("unknown log level '" + std::string(name) + "'").c_str());
}

level_enum LevelArg::get() const { return by_name ? parse_level(name) : level; }

}  // namespace pyspdlog

NB_MODULE(_core, m) {
    using namespace pyspdlog;

    m.doc() = "Low-level nanobind bindings for spdlog. Use the `pyspdlog` package instead.";

    // nanobind reports objects still alive at exit. Objects referenced by
    // daemon threads always are, so the report is noise for users; it stays
    // available for debugging. (NB_DOMAIN keeps this from affecting other
    // nanobind extensions.)
    const char *leak_warnings = std::getenv("PYSPDLOG_LEAK_WARNINGS");
    nb::set_leak_warnings(leak_warnings && *leak_warnings && std::string_view(leak_warnings) != "0");

    nb::exception<spdlog::spdlog_ex>(m, "SpdlogError");

    nb::enum_<level_enum>(m, "Level", nb::is_arithmetic(),
                          "Log severity. Members compare like integers (TRACE < ... < OFF).")
        .value("TRACE", spdlog::level::trace)
        .value("DEBUG", spdlog::level::debug)
        .value("INFO", spdlog::level::info)
        .value("WARN", spdlog::level::warn)
        .value("ERROR", spdlog::level::err)
        .value("CRITICAL", spdlog::level::critical)
        .value("OFF", spdlog::level::off);

    nb::enum_<spdlog::pattern_time_type>(m, "PatternTimeType",
                                         "Clock used for the time fields of a pattern.")
        .value("LOCAL", spdlog::pattern_time_type::local)
        .value("UTC", spdlog::pattern_time_type::utc);

    nb::enum_<spdlog::color_mode>(m, "ColorMode", "When color sinks emit colors.")
        .value("ALWAYS", spdlog::color_mode::always)
        .value("AUTOMATIC", spdlog::color_mode::automatic, "Only when writing to a terminal.")
        .value("NEVER", spdlog::color_mode::never);

    nb::enum_<spdlog::async_overflow_policy>(m, "OverflowPolicy",
                                             "What an AsyncLogger does when its queue is full.")
        .value("BLOCK", spdlog::async_overflow_policy::block,
               "Wait until there is room in the queue.")
        .value("OVERRUN_OLDEST", spdlog::async_overflow_policy::overrun_oldest,
               "Drop the oldest queued message.")
        .value("DISCARD_NEW", spdlog::async_overflow_policy::discard_new,
               "Drop the new message.");

    m.def("level_from_str", &parse_level, "name"_a,
          "Return the Level for a case-insensitive name such as 'info' or 'warning'.");

    m.attr("SPDLOG_VERSION") = std::to_string(SPDLOG_VER_MAJOR) + "." +
                               std::to_string(SPDLOG_VER_MINOR) + "." +
                               std::to_string(SPDLOG_VER_PATCH);

    bind_sinks(m);
    bind_loggers(m);
    bind_registry(m);

    m.def("_release_python_sinks", &release_python_sinks,
          "Internal: drop the Python references held by Python-backed sinks (used at exit).");
}
