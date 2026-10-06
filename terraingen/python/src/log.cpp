#include "bindings.h"

#include "terraingen/util/log.h"

#include <nanobind/stl/function.h>
#include <nanobind/stl/optional.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/string_view.h>

#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace nb = nanobind;
using namespace nb::literals;

namespace terraingen::python {

void BindLog(nb::module_& m) {
    m.def(
        "set_log_level",
        [](std::string_view name) {
            const std::optional<LogLevel> level = ParseLogLevel(name);
            if (!level) {
                throw nb::value_error(
                    "Log level must be trace, debug, info, warning, error or off");
            }
            SetLogLevel(*level);
        },
        "level"_a,
        "Sets the lowest level TerrainGen logs: 'trace', 'debug', 'info', 'warning' (the "
        "default), 'error' or 'off'.");
    m.def(
        "get_log_level", [] { return std::string(LogLevelName(GetLogLevel())); },
        "The lowest level TerrainGen currently logs.");
    m.def(
        "set_log_sink",
        [](std::optional<nb::callable> callback) {
            if (!callback) {
                SetLogSink(nullptr);
                return;
            }
            auto function = std::make_shared<nb::callable>(std::move(*callback));
            SetLogSink([function](const LogRecord& record) {
                nb::gil_scoped_acquire gil;
                try {
                    (*function)(LogLevelName(record.level), record.category, record.message);
                } catch (nb::python_error& error) {
                    error.discard_as_unraisable("TerrainGen log sink");
                }
            });
        },
        "callback"_a.none(),
        "Sends every log record to callback(level, category, message), or restores standard "
        "error when callback is None. The package installs a sink that forwards to the logging "
        "module on import. The callback runs with the GIL held, on whichever thread logged.");
}

}  // namespace terraingen::python
