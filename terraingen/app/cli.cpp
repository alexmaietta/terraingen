/// @file
/// @brief The `terraingen` commands: generate, validate, schema, GUI, version and help.
#include "terraingen/app/cli.h"

#include "terraingen/app/gui/run.h"
#include "terraingen/config/build.h"
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

namespace terraingen::cli {

namespace {

/// Options of the generate command.
struct GenerateArgs {
    std::filesystem::path config;
    std::optional<std::filesystem::path> outputDir;
    LogLevel logLevel = LogLevel::Info;
    unsigned threads = 0;
};

GenerateArgs ParseGenerate(const std::vector<std::string>& args) {
    GenerateArgs parsed;
    bool haveConfig = false;
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& arg = args[i];
        const auto value = [&]() -> const std::string& {
            if (i + 1 >= args.size()) {
                throw std::invalid_argument(arg + " needs a value");
            }
            return args[++i];
        };
        if (arg == "--output-dir") {
            parsed.outputDir = value();
        } else if (arg == "--log-level") {
            const std::string& name = value();
            const std::optional<LogLevel> level = ParseLogLevel(name);
            if (!level) {
                throw std::invalid_argument(
                    "--log-level needs trace, debug, info, warning, error or off, not '" + name +
                    "'");
            }
            parsed.logLevel = *level;
        } else if (arg == "--threads") {
            parsed.threads = ParseThreads(value());
        } else if (arg.rfind("--", 0) == 0) {
            throw std::invalid_argument("Unknown option " + arg);
        } else if (haveConfig) {
            throw std::invalid_argument("Unexpected argument " + arg);
        } else {
            parsed.config = arg;
            haveConfig = true;
        }
    }
    if (!haveConfig) {
        throw std::invalid_argument("Missing the configuration file");
    }
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
        return kExitInvalidConfig;
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
        const TerrainConfig config = LoadConfig(parsed.config);
        const std::filesystem::path inputs = parsed.config.parent_path();
        const GeneratedTerrain terrain = GenerateTerrain(config, inputs, parsed.threads);
        const auto written = WriteOutputs(config, terrain, parsed.outputDir.value_or(inputs));
        out << Summary(terrain) << "\n";
        for (const auto& path : written) {
            out << "Wrote " << path.string() << "\n";
        }
    });
}

int Validate(const std::vector<std::string>& args, std::ostream& out, std::ostream& err) {
    if (args.size() != 2) {
        throw std::invalid_argument("validate needs exactly one configuration file");
    }
    const std::filesystem::path path = args[1];
    return RunReporting(path, err, [&] {
        const TerrainConfig config = LoadConfig(path);
        const ConfigSummary summary = ValidateConfig(config, path.parent_path());
        out << path.string() << " is valid: a " << summary.grid.width << "x" << summary.grid.height
            << " grid";
        if (summary.expectedCraters) {
            out << ", about " << static_cast<long long>(*summary.expectedCraters)
                << " craters expected";
        }
        out << ", " << config.layers.size() << " layers, " << config.filters.size()
            << " filters and " << config.outputs.size() << " outputs\n";
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

std::string Usage() {
    return "Usage:\n"
           "  terraingen CONFIG [--output-dir DIR] [--log-level LEVEL] [--threads N]\n"
           "                         Generate the terrain the JSON file CONFIG describes\n"
           "  terraingen validate CONFIG\n"
           "                         Check CONFIG without generating\n"
           "  terraingen schema      Print every configuration type with its defaults\n"
           "  terraingen [PATH] --gui [--threads N]\n"
           "                         Open the GUI on a configuration or GeoTIFF heights\n"
           "  terraingen -V, --version\n"
           "                         Show version and build information\n"
           "  terraingen -h, --help  Show this message\n";
}

int Run(const std::vector<std::string>& args, std::ostream& out, std::ostream& err) {
    try {
        if (args.empty()) {
            throw std::invalid_argument("Missing a command or configuration file");
        }
        const auto gui = std::find(args.begin(), args.end(), "--gui");
        if (gui != args.end()) {
            std::vector<std::string> rest(args.begin(), gui);
            rest.insert(rest.end(), gui + 1, args.end());
            return RunGui(rest, out, err);
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
            out << ConfigCatalog();
            return kExitSuccess;
        }
        if (first == "validate") {
            return Validate(args, out, err);
        }
        return Generate(args, out, err);
    } catch (const std::invalid_argument& error) {
        err << "terraingen: " << error.what() << "\n\n" << Usage();
        return kExitUsage;
    }
}

}  // namespace terraingen::cli
