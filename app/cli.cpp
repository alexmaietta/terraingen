// SPDX-License-Identifier: BSD-3-Clause

#include "app/cli.h"

#include "config/spec.h"
#include "terraingen/config/generate.h"
#include "terraingen/config/json.h"
#include "terraingen/config/validate.h"
#include "terraingen/util/build_info.h"
#include "terraingen/util/log.h"
#include "terraingen/util/version.h"

#include <algorithm>
#include <cstddef>
#include <exception>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace terraingen::cli {

namespace {

/// Options of the generate command.
struct GenerateArgs {
    std::filesystem::path config;
    std::optional<std::filesystem::path> outputDir;
    LogLevel logLevel = LogLevel::Info;
    unsigned threads = 0;
    std::optional<Backend> backend = std::nullopt;
};

/// The arguments of a command, read in order.
class ArgCursor {
  public:
    explicit ArgCursor(const std::vector<std::string>& args) : args_(args) {}

    /// Whether every argument has been read.
    bool Done() const {
        return next_ >= args_.size();
    }

    /// The next argument.
    const std::string& Next() {
        return args_[next_++];
    }

    /// The value after the option `option`.
    /// @throws std::invalid_argument when `option` is the last argument.
    const std::string& Value(const std::string& option) {
        if (Done()) {
            throw std::invalid_argument(option + " needs a value");
        }
        return Next();
    }

  private:
    const std::vector<std::string>& args_;
    std::size_t next_ = 0;
};

/// Whether `arg` is an option rather than a path.
bool IsOption(const std::string& arg) {
    return arg.rfind("--", 0) == 0;
}

/// Reads `arg` if it is one of the options every generating command takes, `--threads N` and
/// `--backend NAME`, taking its value from `cursor`; false for any other argument.
bool ReadRunOption(const std::string& arg, ArgCursor& cursor, unsigned& threads,
                   std::optional<Backend>& backend) {
    if (arg == "--threads") {
        threads = ParseThreads(cursor.Value(arg));
    } else if (arg == "--backend") {
        backend = ParseBackend(cursor.Value(arg));
    } else {
        return false;
    }
    return true;
}

/// Sets the single positional argument `path` to `arg`.
/// @throws std::invalid_argument when `path` is already set.
void ReadPath(const std::string& arg, std::optional<std::string>& path) {
    if (path) {
        throw std::invalid_argument("Unexpected argument " + arg);
    }
    path = arg;
}

/// `names` as a message lists them, as in "a, b or c".
template <typename Names> std::string ListNames(const Names& names) {
    std::string list;
    for (std::size_t i = 0; i < names.size(); ++i) {
        list += (i == 0 ? "" : i + 1 == names.size() ? " or " : ", ");
        list += names[i];
    }
    return list;
}

/// Parses a `--log-level` value.
/// @throws std::invalid_argument for a name `ParseLogLevel` does not read.
LogLevel ParseLevel(const std::string& name) {
    if (const std::optional<LogLevel> level = ParseLogLevel(name)) {
        return *level;
    }
    std::vector<std::string_view> names;
    for (const LogLevel level : kLogLevels) {
        names.push_back(LogLevelName(level));
    }
    throw std::invalid_argument("--log-level needs " + ListNames(names) + ", not '" + name + "'");
}

GenerateArgs ParseGenerate(const std::vector<std::string>& args) {
    GenerateArgs parsed;
    std::optional<std::string> config;
    ArgCursor cursor(args);
    while (!cursor.Done()) {
        const std::string& arg = cursor.Next();
        if (ReadRunOption(arg, cursor, parsed.threads, parsed.backend)) {
            continue;
        }
        if (arg == "--output-dir") {
            parsed.outputDir = cursor.Value(arg);
        } else if (arg == "--log-level") {
            parsed.logLevel = ParseLevel(cursor.Value(arg));
        } else if (IsOption(arg)) {
            throw std::invalid_argument("Unknown option " + arg);
        } else {
            ReadPath(arg, config);
        }
    }
    if (!config) {
        throw std::invalid_argument("Missing the configuration file");
    }
    parsed.config = *config;
    return parsed;
}

/// Restores the log level it was made with when it goes out of scope.
class LogLevelScope {
  public:
    explicit LogLevelScope(LogLevel level) : previous_(GetLogLevel()) {
        SetLogLevel(level);
    }
    LogLevelScope(const LogLevelScope&) = delete;
    LogLevelScope& operator=(const LogLevelScope&) = delete;
    ~LogLevelScope() {
        SetLogLevel(previous_);
    }

  private:
    LogLevel previous_;
};

/// Runs `command`, reporting a configuration error against the file `config` and any other error
/// as terraingen's, and returns the exit code.
template <typename Command>
int RunReporting(const std::filesystem::path& config, std::ostream& err, Command command) {
    try {
        command();
    } catch (const ConfigError& error) {
        err << config.string() << ": " << error.what() << "\n";
        return kExitFailure;
    } catch (const std::exception& error) {
        err << "terraingen: " << error.what() << "\n";
        return kExitFailure;
    }
    return kExitSuccess;
}

int Generate(const std::vector<std::string>& args, std::ostream& out, std::ostream& err) {
    const GenerateArgs parsed = ParseGenerate(args);
    const LogLevelScope level(parsed.logLevel);
    return RunReporting(parsed.config, err, [&] {
        const TerrainGenConfig config = LoadConfig(parsed.config);
        const std::filesystem::path inputs = parsed.config.parent_path();
        const GeneratedOutputs generated =
            GenerateFromConfig(config, inputs, parsed.threads, parsed.backend);
        const std::filesystem::path outputs = parsed.outputDir.value_or(inputs);
        const Stopwatch stopwatch;
        const auto written = WriteOutputs(config, generated, outputs);
        out << Summary(generated) << "\n";
        out << "Wrote " << written.size() << (written.size() == 1 ? " file" : " files") << " to "
            << (outputs.empty() ? std::filesystem::path(".") : outputs).string() << " in "
            << FormatDuration(stopwatch.Milliseconds()) << "\n";
    });
}

/// Prints each backend with its device or why it is unavailable, then the default backend.
int ListBackends(std::ostream& out, std::ostream& err) {
    for (const BackendStatus& status : Backends()) {
        out << BackendName(status.backend) << ": "
            << (status.available ? "available on " + status.device
                                 : "unavailable: " + status.reason)
            << "\n";
    }
    try {
        out << "default: " << BackendName(DefaultBackend()) << "\n";
    } catch (const std::invalid_argument& error) {
        err << "terraingen: " << error.what() << "\n";
        return kExitFailure;
    }
    return kExitSuccess;
}

int Validate(const std::vector<std::string>& args, std::ostream& out, std::ostream& err) {
    if (args.size() != 2) {
        throw std::invalid_argument("validate needs exactly one configuration file");
    }
    const std::filesystem::path path = args[1];
    return RunReporting(path, err, [&] {
        const TerrainGenConfig config = LoadConfig(path);
        const ConfigSummary summary = ValidateConfig(config, path.parent_path());
        out << path.string() << " is valid: ";
        std::vector<RasterGrid> grids;
        for (const std::optional<RasterGrid>& grid : summary.outputGrids) {
            if (grid && std::find(grids.begin(), grids.end(), *grid) == grids.end()) {
                out << (grids.empty() ? "a " : " and ") << grid->width << "x" << grid->height;
                grids.push_back(*grid);
            }
        }
        out << (grids.empty() ? "no grids" : grids.size() == 1 ? " grid" : " grids");
        if (summary.expectedCraters) {
            out << ", about " << static_cast<long long>(*summary.expectedCraters)
                << " craters expected";
        }
        out << ", " << config.Spec().layers.size() << " layers and " << config.Spec().outputs.size()
            << " outputs\n";
    });
}

}  // namespace

unsigned ParseThreads(const std::string& text) {
    std::size_t used = 0;
    unsigned long value = 0;
    try {
        value = std::stoul(text, &used);
    } catch (const std::exception&) {
        used = 0;
    }
    if (used == 0 || used != text.size() || text.front() == '-' || value > 4096) {
        throw std::invalid_argument(
            "--threads needs a count from 0 (every hardware thread) to 4096, not '" + text + "'");
    }
    return static_cast<unsigned>(value);
}

Backend ParseBackend(const std::string& text) {
    if (const std::optional<Backend> backend = BackendNamed(text)) {
        return *backend;
    }
    throw std::invalid_argument("--backend needs " + BackendNameList() + ", not '" + text + "'");
}

std::string Usage() {
    std::string backends;
    for (const std::string_view name : kBackendNames) {
        backends += (backends.empty() ? "" : "|") + std::string(name);
    }
    std::string levels;
    for (const LogLevel level : kLogLevels) {
        levels += (levels.empty() ? "" : "|") + std::string(LogLevelName(level));
    }
    return "Usage:\n"
           "  terraingen CONFIG [--output-dir DIR] [--log-level LEVEL] [--threads N]\n"
           "                    [--backend " +
           backends +
           "]\n"
           "                         Generate the terrain the JSON file CONFIG describes\n"
           "  terraingen validate CONFIG\n"
           "                         Check CONFIG without generating\n"
           "  terraingen schema      Print the configuration schema, for editors\n"
           "  terraingen backends    Show where terrains can be evaluated here, and the\n"
           "                         default\n"
           "  terraingen -V, --version\n"
           "                         Show version and build information\n"
           "  terraingen -h, --help  Show this message\n"
           "\n"
           "Options:\n"
           "  --output-dir DIR       Write outputs with relative paths under DIR rather than\n"
           "                         CONFIG's folder\n"
           "  --log-level LEVEL      Log at LEVEL, one of\n"
           "                         " +
           levels +
           ". The default is info\n"
           "  --threads N            Generate on N threads, or 0 for every hardware thread,\n"
           "                         the default. The result does not depend on it\n"
           "  --backend NAME         Evaluate the heights on NAME. The default is cpu, or\n"
           "                         TERRAINGEN_BACKEND when set. gpu picks the best GPU and\n"
           "                         auto picks the best GPU or the CPU\n"
           "\n"
           "terraingen exits with 0 on success, 1 when generating or validating fails and 2\n"
           "for a usage error.\n";
}

int Run(const std::vector<std::string>& args, std::ostream& out, std::ostream& err) {
    try {
        if (args.empty()) {
            throw std::invalid_argument("Missing a command or configuration file");
        }
        const std::string& first = args.front();
        if (first == "--help" || first == "-h") {
            out << Usage();
            return kExitSuccess;
        }
        if (first == "--version" || first == "-V") {
            const Version version = GetVersion();
            const BuildInfo build = GetBuildInfo();
            out << "TerrainGen " << version.major << "." << version.minor << "." << version.patch
                << " (" << build.gitSha << (build.gitDirty ? "-dirty" : "") << ", "
                << build.buildDate << ")\n";
            return kExitSuccess;
        }
        if (first == "schema") {
            if (args.size() != 1) {
                throw std::invalid_argument("schema takes no arguments");
            }
            out << ConfigSchema();
            return kExitSuccess;
        }
        if (first == "backends") {
            if (args.size() != 1) {
                throw std::invalid_argument("backends takes no arguments");
            }
            return ListBackends(out, err);
        }
        if (first == "validate") {
            return Validate(args, out, err);
        }
        return Generate(args, out, err);
    } catch (const std::invalid_argument& error) {
        err << "terraingen: " << error.what() << "\nRun 'terraingen --help' to see the usage.\n";
        return kExitUsage;
    }
}

}  // namespace terraingen::cli
