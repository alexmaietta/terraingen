#include "terraingen/app/cli.h"
#include "terraingen/app/viewer/view.h"
#include "terraingen/raster/geotiff.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using terraingen::cli::kExitInvalidConfig;
using terraingen::cli::kExitSuccess;
using terraingen::cli::kExitUsage;

/// What one run of the CLI printed and returned.
struct Result {
    int code = 0;
    std::string out;
    std::string err;
};

Result Cli(const std::vector<std::string>& args) {
    std::ostringstream out;
    std::ostringstream err;
    const int code = terraingen::cli::Run(args, out, err);
    return {code, out.str(), err.str()};
}

/// A fresh directory for one test.
std::filesystem::path Directory(const std::string& name) {
    const std::filesystem::path directory =
        std::filesystem::path(::testing::TempDir()) / ("terraingen_cli_" + name);
    std::filesystem::remove_all(directory);
    std::filesystem::create_directories(directory);
    return directory;
}

std::filesystem::path Write(const std::filesystem::path& path, const std::string& text) {
    std::ofstream(path) << text;
    return path;
}

const char* const kConfig = R"({
  "schema_version": 1,
  "source": {"type": "flat", "width": 65, "height": 49, "pixel_size": 2},
  "layers": [
    {"type": "noise", "stack": [{"wavelength": 60, "amplitude": 3, "octaves": 3}]},
    {"type": "craters", "min_diameter": 6, "max_diameter": 40,
     "density": {"type": "lunar_surface", "age_gyr": 3.5}}
  ],
  "outputs": [{"type": "heightmap", "path": "heights.tif"},
              {"type": "crater_list", "path": "craters.csv"}]
})";

}  // namespace

TEST(Cli, PrintsHelpAndVersion) {
    const Result help = Cli({"--help"});
    EXPECT_EQ(help.code, kExitSuccess);
    EXPECT_NE(help.out.find("terraingen validate CONFIG"), std::string::npos);
    EXPECT_EQ(Cli({"-h"}).out, help.out);
    const Result version = Cli({"--version"});
    EXPECT_EQ(version.code, kExitSuccess);
    EXPECT_EQ(version.out.rfind("TerrainGen ", 0), 0u);
    EXPECT_EQ(Cli({"-V"}).out, version.out);
    EXPECT_NE(help.out.find("-V, --version"), std::string::npos);
    EXPECT_NE(help.out.find("-h, --help"), std::string::npos);
}

TEST(Cli, PrintsTheConfigCatalog) {
    const Result schema = Cli({"schema"});
    ASSERT_EQ(schema.code, kExitSuccess);
    const nlohmann::json catalog = nlohmann::json::parse(schema.out);
    EXPECT_TRUE(catalog.contains("layers"));
    EXPECT_TRUE(catalog["models"].contains("density"));
    EXPECT_EQ(Cli({"schema", "extra"}).code, kExitUsage);
}

TEST(Cli, RejectsBadArguments) {
    for (const std::vector<std::string>& args :
         std::vector<std::vector<std::string>>{{},
                                               {"--bogus"},
                                               {"a.json", "b.json"},
                                               {"--output-dir"},
                                               {"a.json", "--log-level", "loud"},
                                               {"a.json", "--threads", "-1"},
                                               {"a.json", "--threads", "two"},
                                               {"a.json", "--threads", "5000"},
                                               {"a.json", "--threads", ""},
                                               {"--threads", "2"},
                                               {"validate"},
                                               {"validate", "a.json", "b.json"}}) {
        const Result result = Cli(args);
        EXPECT_EQ(result.code, kExitUsage) << ::testing::PrintToString(args);
        EXPECT_NE(result.err.find("Usage:"), std::string::npos);
    }
}

TEST(Cli, ParsesThreadCounts) {
    EXPECT_EQ(terraingen::cli::ParseThreads("0"), 0u);
    EXPECT_EQ(terraingen::cli::ParseThreads("4096"), 4096u);
    for (const char* bad : {"", "-1", "4097", "two", "3x"}) {
        EXPECT_THROW(terraingen::cli::ParseThreads(bad), std::invalid_argument) << bad;
    }
}

TEST(Cli, ViewRejectsBadArgumentsOrExplainsHowToAddTheViewer) {
    EXPECT_NE(Cli({"--help"}).out.find("terraingen view [PATH]"), std::string::npos);
    for (const std::vector<std::string>& args :
         std::vector<std::vector<std::string>>{{"view", "--bogus"},
                                               {"view", "a.json", "b.json"},
                                               {"view", "--threads"},
                                               {"view", "a.json", "--threads", "lots"}}) {
        EXPECT_EQ(Cli(args).code, kExitUsage) << ::testing::PrintToString(args);
    }
    if (!terraingen::cli::ViewerAvailable()) {
        const Result result = Cli({"view", "a.json"});
        EXPECT_EQ(result.code, kExitUsage);
        EXPECT_NE(result.err.find("-DTERRAINGEN_BUILD_VIEWER=ON"), std::string::npos);
        EXPECT_NE(result.err.find("TERRAINGEN_ERNIE_DIR"), std::string::npos);
    }
}

TEST(Cli, ValidatesAConfigAndReportsItsGridAndCraters) {
    const auto directory = Directory("validate");
    const auto config = Write(directory / "terrain.json", kConfig);
    const Result valid = Cli({"validate", config.string()});
    EXPECT_EQ(valid.code, kExitSuccess) << valid.err;
    EXPECT_NE(valid.out.find("is valid: a 65x49 grid, about "), std::string::npos) << valid.out;
    EXPECT_NE(valid.out.find("2 layers, 0 filters and 2 outputs"), std::string::npos);
    EXPECT_FALSE(std::filesystem::exists(directory / "heights.tif"));

    const auto bad = Write(directory / "bad.json",
                           R"({"schema_version": 1, "layers": [{"type": "noise", "stack": []}]})");
    const Result invalid = Cli({"validate", bad.string()});
    EXPECT_EQ(invalid.code, kExitInvalidConfig);
    EXPECT_NE(invalid.err.find("layers[0]"), std::string::npos) << invalid.err;
    EXPECT_EQ(Cli({"validate", (directory / "missing.json").string()}).code, kExitInvalidConfig);

    const auto raster = Write(directory / "raster.json",
                              R"({"schema_version": 1,
                                  "source": {"type": "raster", "path": "nowhere.tif"}})");
    EXPECT_EQ(Cli({"validate", raster.string()}).code, kExitInvalidConfig);
}

TEST(Cli, GeneratesAConfigsOutputsIntoTheChosenFolder) {
    const auto directory = Directory("generate");
    const auto config = Write(directory / "terrain.json", kConfig);
    const Result beside = Cli({config.string(), "--log-level", "off", "--threads", "2"});
    ASSERT_EQ(beside.code, kExitSuccess) << beside.err;
    EXPECT_NE(beside.out.find("Generated a 65x49 terrain with "), std::string::npos);
    EXPECT_NE(beside.out.find("Wrote "), std::string::npos);
    const auto heights = terraingen::ReadGeoTiff(directory / "heights.tif").raster;
    EXPECT_EQ(heights.Width(), 65u);
    EXPECT_TRUE(std::filesystem::exists(directory / "craters.csv"));

    const auto elsewhere = Directory("generate_out");
    const Result moved = Cli({config.string(), "--output-dir", elsewhere.string()});
    ASSERT_EQ(moved.code, kExitSuccess) << moved.err;
    EXPECT_TRUE(std::filesystem::exists(elsewhere / "heights.tif"));
    const auto other = terraingen::ReadGeoTiff(elsewhere / "heights.tif").raster;
    EXPECT_TRUE(std::ranges::equal(other.Values(), heights.Values()));
}

TEST(Cli, ReportsConfigAndOutputErrorsWhenGenerating) {
    const auto directory = Directory("generate_errors");
    const auto bad = Write(directory / "bad.json", R"({"schema_version": 99})");
    const Result invalid = Cli({bad.string()});
    EXPECT_EQ(invalid.code, kExitInvalidConfig);
    EXPECT_NE(invalid.err.find("bad.json: "), std::string::npos) << invalid.err;

    const auto config = Write(directory / "terrain.json", kConfig);
    const auto blocked = directory / "blocked";
    Write(blocked, "a file, not a folder");
    const Result unwritable = Cli({config.string(), "--output-dir", blocked.string()});
    EXPECT_EQ(unwritable.code, kExitInvalidConfig);
    EXPECT_EQ(unwritable.err.rfind("terraingen: ", 0), 0u) << unwritable.err;
}
