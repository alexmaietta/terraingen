#include "cli.h"

#include "terraingen/core/build_info.h"
#include "terraingen/core/log.h"
#include "terraingen/core/version.h"
#include "terraingen/pipeline/config_json.h"
#include "terraingen/pipeline/run.h"

#include <cstddef>
#include <exception>
#include <filesystem>
#include <optional>
#include <stdexcept>

namespace terraingen::cli {

namespace {

/// Options of the generate command.
struct GenerateArgs {
    std::filesystem::path config;
    std::optional<std::filesystem::path> outputDir;
    LogLevel logLevel = LogLevel::Info;
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
                throw std::invalid_argument("--log-level needs trace, debug, info, warning, error "
                                            "or off, not '" +
                                            name + "'");
            }
            parsed.logLevel = *level;
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

/// A configuration showing every section, with an example crater list output.
TerrainConfig SchemaExample() {
    TerrainConfig config;
    config.outputs.push_back(CraterListOutput{"craters.csv", CraterListFormat::Csv});
    return config;
}

int Generate(const std::vector<std::string>& args, std::ostream& out, std::ostream& err) {
    const GenerateArgs parsed = ParseGenerate(args);
    const LogLevel previous = GetLogLevel();
    SetLogLevel(parsed.logLevel);
    try {
        const TerrainConfig config = LoadConfig(parsed.config);
        const GeneratedTerrain terrain = GenerateTerrain(config);
        const std::filesystem::path directory =
            parsed.outputDir.value_or(parsed.config.parent_path());
        const auto written = WriteOutputs(config, terrain, directory);
        out << "Generated " << terrain.heights.Width() << "x" << terrain.heights.Height()
            << " terrain with " << terrain.craters.Size() << " craters\n";
        for (const auto& path : written) {
            out << "Wrote " << path.string() << "\n";
        }
    } catch (const ConfigError& error) {
        SetLogLevel(previous);
        err << parsed.config.string() << ": " << error.what() << "\n";
        return kExitInvalidConfig;
    } catch (const std::runtime_error& error) {
        SetLogLevel(previous);
        err << "terraingen: " << error.what() << "\n";
        return kExitInvalidConfig;
    }
    SetLogLevel(previous);
    return kExitSuccess;
}

int Validate(const std::vector<std::string>& args, std::ostream& out, std::ostream& err) {
    if (args.size() != 2) {
        throw std::invalid_argument("validate needs exactly one configuration file");
    }
    const std::filesystem::path path = args[1];
    try {
        const TerrainConfig config = LoadConfig(path);
        ValidateConfig(config);
        out << path.string() << " is valid: " << config.grid.width << "x" << config.grid.height
            << " grid";
        if (config.craters && config.craters->generate) {
            const CraterModels models = BuildCraterModels(*config.craters);
            const Bounds2d bounds = GridBounds(config.grid);
            const double area = (bounds.maxX - bounds.minX) * (bounds.maxY - bounds.minY);
            const double expected = models.density->ExpectedCount(area, config.craters->minDiameter,
                                                                  config.craters->maxDiameter);
            out << ", about " << static_cast<long long>(expected)
                << " craters expected over the grid";
        }
        out << "\n";
    } catch (const ConfigError& error) {
        err << path.string() << ": " << error.what() << "\n";
        return kExitInvalidConfig;
    }
    return kExitSuccess;
}

}  // namespace

std::string Usage() {
    return "Usage:\n"
           "  terraingen CONFIG [--output-dir DIR] [--log-level LEVEL]\n"
           "                         Generate the terrain the JSON file CONFIG describes\n"
           "  terraingen validate CONFIG\n"
           "                         Check CONFIG without generating\n"
           "  terraingen schema      Print a complete configuration with every default\n"
           "  terraingen --version   Show version and build information\n"
           "  terraingen --help      Show this message\n"
           "\n"
           "Relative output paths resolve against DIR, or the folder holding CONFIG.\n"
           "LEVEL is trace, debug, info (default), warning, error or off.\n";
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
        if (first == "--version") {
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
            out << ConfigToJson(SchemaExample());
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
