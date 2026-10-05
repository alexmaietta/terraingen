#include "cli.h"

#include "terraingen/core/log.h"
#include "terraingen/pipeline/config_json.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using terraingen::cli::kExitInvalidConfig;
using terraingen::cli::kExitSuccess;
using terraingen::cli::kExitUsage;

namespace {

struct Result {
    int code = 0;
    std::string out;
    std::string err;
};

Result Invoke(const std::vector<std::string>& args) {
    std::ostringstream out;
    std::ostringstream err;
    const int code = terraingen::cli::Run(args, out, err);
    return {code, out.str(), err.str()};
}

/// Writes a small configuration with a crater list output to a fresh folder and returns its path.
std::filesystem::path WriteConfig(const std::string& name, const std::string& extra = "") {
    const auto directory = std::filesystem::path(::testing::TempDir()) / name;
    std::filesystem::remove_all(directory);
    std::filesystem::create_directories(directory);
    const auto path = directory / "config.json";
    std::ofstream file(path);
    file << R"({"schema_version": 1, "grid": {"width": 33, "height": 33},
                "craters": {"min_diameter": 8, "max_diameter": 40)" +
                extra + R"(},
                "outputs": [{"type": "crater_list", "path": "craters.csv"}]})";
    return path;
}

}  // namespace

TEST(Cli, PrintsHelpAndVersion) {
    const Result help = Invoke({"--help"});
    EXPECT_EQ(help.code, kExitSuccess);
    for (const char* text :
         {"terraingen CONFIG", "validate", "schema", "--output-dir", "--log-level"}) {
        EXPECT_NE(help.out.find(text), std::string::npos) << text;
    }
    EXPECT_EQ(Invoke({"-h"}).out, help.out);
    const Result version = Invoke({"--version"});
    EXPECT_EQ(version.code, kExitSuccess);
    EXPECT_EQ(version.out.rfind("TerrainGen ", 0), 0u);
}

TEST(Cli, SchemaPrintsAValidCompleteConfiguration) {
    const Result schema = Invoke({"schema"});
    ASSERT_EQ(schema.code, kExitSuccess);
    const terraingen::TerrainConfig config = terraingen::ParseConfig(schema.out);
    EXPECT_TRUE(config.craters.has_value());
    EXPECT_EQ(config.outputs.size(), 1u);
    EXPECT_EQ(Invoke({"schema", "extra"}).code, kExitUsage);
}

TEST(Cli, ValidateReportsValidAndInvalidConfigurations) {
    const auto good = WriteConfig("terraingen_cli_valid");
    const Result valid = Invoke({"validate", good.string()});
    EXPECT_EQ(valid.code, kExitSuccess) << valid.err;
    EXPECT_NE(valid.out.find("is valid: 33x33 grid, about "), std::string::npos) << valid.out;

    const auto bad = WriteConfig("terraingen_cli_invalid", R"(, "seed": -1)");
    const Result invalid = Invoke({"validate", bad.string()});
    EXPECT_EQ(invalid.code, kExitInvalidConfig);
    EXPECT_NE(invalid.err.find("craters.seed: expected a non-negative integer"), std::string::npos)
        << invalid.err;
    EXPECT_EQ(Invoke({"validate"}).code, kExitUsage);
    EXPECT_EQ(Invoke({"validate", "missing.json"}).code, kExitInvalidConfig);
}

TEST(Cli, GeneratesAndWritesOutputsNextToTheConfigOrToAnOutputFolder) {
    const auto config = WriteConfig("terraingen_cli_generate");
    const Result result = Invoke({config.string(), "--log-level", "off"});
    ASSERT_EQ(result.code, kExitSuccess) << result.err;
    EXPECT_NE(result.out.find("Generated 33x33 terrain with "), std::string::npos) << result.out;
    EXPECT_TRUE(std::filesystem::exists(config.parent_path() / "craters.csv"));

    const auto elsewhere = config.parent_path() / "elsewhere";
    const Result redirected =
        Invoke({config.string(), "--output-dir", elsewhere.string(), "--log-level", "off"});
    ASSERT_EQ(redirected.code, kExitSuccess) << redirected.err;
    EXPECT_TRUE(std::filesystem::exists(elsewhere / "craters.csv"));
    EXPECT_EQ(terraingen::GetLogLevel(), terraingen::LogLevel::Warning);
}

TEST(Cli, ReportsConfigurationAndUsageErrors) {
    const auto bad = WriteConfig("terraingen_cli_bad", R"(, "max_diameter": 2)");
    const Result invalid = Invoke({bad.string(), "--log-level", "off"});
    EXPECT_EQ(invalid.code, kExitInvalidConfig);
    EXPECT_NE(invalid.err.find("craters.max_diameter"), std::string::npos) << invalid.err;
    const auto blocked = WriteConfig("terraingen_cli_blocked");
    std::ofstream(blocked.parent_path() / "blocker") << "a file, not a folder";
    const Result unwritable = Invoke({blocked.string(), "--output-dir",
                                      (blocked.parent_path() / "blocker").string(), "--log-level",
                                      "off"});
    EXPECT_EQ(unwritable.code, kExitInvalidConfig);
    EXPECT_EQ(unwritable.err.rfind("terraingen: ", 0), 0u) << unwritable.err;
    EXPECT_EQ(Invoke({}).code, kExitUsage);
    EXPECT_EQ(Invoke({"a.json", "b.json"}).code, kExitUsage);
    EXPECT_EQ(Invoke({"a.json", "--frobnicate"}).code, kExitUsage);
    EXPECT_EQ(Invoke({"a.json", "--output-dir"}).code, kExitUsage);
    EXPECT_EQ(Invoke({"a.json", "--log-level", "loud"}).code, kExitUsage);
    EXPECT_EQ(Invoke({"--log-level", "off"}).code, kExitUsage);
    const Result usage = Invoke({});
    EXPECT_NE(usage.err.find("Usage:"), std::string::npos);
}
