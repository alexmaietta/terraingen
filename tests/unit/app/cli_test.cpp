#include "terraingen/app/cli.h"
#include "terraingen/app/gui/run.h"
#include "terraingen/raster/geotiff/geotiff.h"

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

using terraingen::cli::kExitFailure;
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
  "layers": [
    {"type": "noise", "stack": [{"wavelength": 60, "amplitude": 3, "octaves": 3}]},
    {"type": "craters", "min_diameter": 6, "max_diameter": 40,
     "density": {"type": "minimum", "distributions": [
         {"type": "production_surface", "production": "extended_neukum", "chronology": "neukum",
          "age_gyr": 3.5},
         {"type": "trask"}]},
     "ages": {"type": "steady_state", "age_gyr": 3.5, "production": "extended_neukum",
              "equilibrium": "trask", "chronology": "neukum"}}
  ],
  "outputs": [{"type": "heightmap", "path": "heights.tif", "region": {"width": 65, "height": 49}},
              {"type": "crater_list", "path": "craters.csv", "region": {"width": 65, "height": 49}}]
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
                                               {"a.json", "--backend", "opencl"},
                                               {"a.json", "--backend"},
                                               {"validate"},
                                               {"validate", "a.json", "b.json"}}) {
        const Result result = Cli(args);
        EXPECT_EQ(result.code, kExitUsage) << ::testing::PrintToString(args);
        EXPECT_NE(result.err.find("\nRun 'terraingen --help' to see the usage.\n"),
                  std::string::npos);
        EXPECT_EQ(result.err.find("Usage:"), std::string::npos) << "the usage only on --help";
    }
    EXPECT_EQ(Cli({}).err, "terraingen: Missing a command or configuration file\n"
                           "Run 'terraingen --help' to see the usage.\n");
}

TEST(Cli, NamesTheChoicesOfBadOptionValues) {
    EXPECT_NE(Cli({"a.json", "--log-level", "loud"})
                  .err.find("--log-level needs trace, debug, info, warning, error or off, not "
                            "'loud'"),
              std::string::npos);
    EXPECT_NE(Cli({"a.json", "--backend", "opencl"})
                  .err.find("--backend needs auto, cpu, gpu, vulkan, cuda or metal, not 'opencl'"),
              std::string::npos);
    EXPECT_NE(Cli({"a.json", "--output-dir"}).err.find("--output-dir needs a value"),
              std::string::npos);
    EXPECT_NE(Cli({"a.json", "b.json"}).err.find("Unexpected argument b.json"), std::string::npos);
}

TEST(Cli, ParsesThreadCounts) {
    EXPECT_EQ(terraingen::cli::ParseThreads("0"), 0u);
    EXPECT_EQ(terraingen::cli::ParseThreads("4096"), 4096u);
    for (const char* bad : {"", "-1", "4097", "two", "3x"}) {
        EXPECT_THROW(terraingen::cli::ParseThreads(bad), std::invalid_argument) << bad;
    }
}

TEST(Cli, ParsesBackendNames) {
    EXPECT_EQ(terraingen::cli::ParseBackend("cpu"), terraingen::Backend::Cpu);
    EXPECT_EQ(terraingen::cli::ParseBackend("Vulkan"), terraingen::Backend::Vulkan);
    EXPECT_EQ(terraingen::cli::ParseBackend("auto"), terraingen::Backend::Auto);
    EXPECT_EQ(terraingen::cli::ParseBackend("GPU"), terraingen::Backend::Gpu);
    EXPECT_EQ(terraingen::cli::ParseBackend("cuda"), terraingen::Backend::Cuda);
    EXPECT_EQ(terraingen::cli::ParseBackend("metal"), terraingen::Backend::Metal);
    EXPECT_THROW(terraingen::cli::ParseBackend("opencl"), std::invalid_argument);
    EXPECT_NE(Cli({"--help"}).out.find("--backend auto|cpu|gpu|vulkan|cuda|metal"),
              std::string::npos);
}

TEST(Cli, ListsTheBackendsAndTheDefault) {
    const Result result = Cli({"backends"});
    ASSERT_EQ(result.code, kExitSuccess) << result.err;
    EXPECT_EQ(result.out.rfind("cpu: available on host\nvulkan: ", 0), 0u) << result.out;
    EXPECT_NE(result.out.find("\ndefault: "), std::string::npos);
    const terraingen::BackendStatus vulkan = terraingen::Backends()[1];
    EXPECT_NE(result.out.find(vulkan.available ? "available on " + vulkan.device
                                               : "unavailable: " + vulkan.reason),
              std::string::npos);
    EXPECT_NE(Cli({"--help"}).out.find("terraingen backends"), std::string::npos);
    EXPECT_EQ(Cli({"backends", "extra"}).code, kExitUsage);
}

TEST(Cli, GeneratesOnTheChosenBackend) {
    const auto directory = Directory("generate_backend");
    const auto config = Write(directory / "terrain.json", kConfig);
    const Result cpu = Cli({config.string(), "--log-level", "off", "--backend", "cpu"});
    ASSERT_EQ(cpu.code, kExitSuccess) << cpu.err;
    EXPECT_NE(cpu.out.find(" s on cpu\n"), std::string::npos) << cpu.out;
    const auto onCpu = terraingen::ReadGeoTiff(directory / "heights.tif").raster;
    const Result automatic = Cli({config.string(), "--log-level", "off", "--backend", "auto"});
    ASSERT_EQ(automatic.code, kExitSuccess) << automatic.err;
    const auto chosen = terraingen::ReadGeoTiff(directory / "heights.tif").raster;
    ASSERT_EQ(chosen.Size(), onCpu.Size());
    for (std::size_t i = 0; i < chosen.Size(); ++i) {
        ASSERT_NEAR(chosen.Values()[i], onCpu.Values()[i], 1e-3) << i;
    }
}

TEST(Cli, ParsesTheGuiArguments) {
    const terraingen::cli::GuiArgs none = terraingen::cli::ParseGuiArgs({});
    EXPECT_TRUE(none.open.empty());
    EXPECT_EQ(none.threads, 0u);
    EXPECT_FALSE(none.backend.has_value());
    const terraingen::cli::GuiArgs all =
        terraingen::cli::ParseGuiArgs({"--backend", "vulkan", "site.json", "--threads", "3"});
    EXPECT_EQ(all.open, "site.json");
    EXPECT_EQ(all.threads, 3u);
    EXPECT_EQ(all.backend, terraingen::Backend::Vulkan);
    EXPECT_THROW(terraingen::cli::ParseGuiArgs({"a.json", "b.json"}), std::invalid_argument);
    EXPECT_THROW(terraingen::cli::ParseGuiArgs({"--threads"}), std::invalid_argument);
    EXPECT_NE(
        Cli({"--help"}).out.find("--gui [--threads N] [--backend auto|cpu|gpu|vulkan|cuda|metal]"),
        std::string::npos);
}

TEST(Cli, GuiRejectsBadArgumentsOrExplainsHowToAddTheGui) {
    EXPECT_NE(Cli({"--help"}).out.find("terraingen [PATH] --gui"), std::string::npos);
    for (const std::vector<std::string>& args :
         std::vector<std::vector<std::string>>{{"--gui", "--bogus"},
                                               {"a.json", "b.json", "--gui"},
                                               {"--gui", "--threads"},
                                               {"a.json", "--gui", "--output-dir", "out"},
                                               {"a.json", "--threads", "lots", "--gui"},
                                               {"a.json", "--gui", "--backend", "opencl"},
                                               {"--gui", "--backend"}}) {
        EXPECT_EQ(Cli(args).code, kExitUsage) << ::testing::PrintToString(args);
    }
    if (!terraingen::cli::GuiAvailable()) {
        const Result result = Cli({"a.json", "--gui"});
        EXPECT_EQ(result.code, kExitUsage);
        EXPECT_NE(result.err.find("-DTERRAINGEN_ERNIE_DIR="), std::string::npos);
    }
}

TEST(Cli, ValidatesAConfigAndReportsItsGridAndCraters) {
    const auto directory = Directory("validate");
    const auto config = Write(directory / "terrain.json", kConfig);
    const Result valid = Cli({"validate", config.string()});
    EXPECT_EQ(valid.code, kExitSuccess) << valid.err;
    EXPECT_NE(valid.out.find("is valid: a 65x49 grid, about "), std::string::npos) << valid.out;
    EXPECT_NE(valid.out.find("2 layers and 2 outputs"), std::string::npos);
    EXPECT_FALSE(std::filesystem::exists(directory / "heights.tif"));

    const auto flat = Write(directory / "flat.json", R"({"schema_version": 1})");
    const Result plain = Cli({"validate", flat.string()});
    EXPECT_EQ(plain.code, kExitSuccess) << plain.err;
    EXPECT_EQ(plain.out, flat.string() + " is valid: no grids, 0 layers and 0 outputs\n");

    const auto bad = Write(directory / "bad.json",
                           R"({"schema_version": 1, "layers": [{"type": "noise", "stack": []}]})");
    const Result invalid = Cli({"validate", bad.string()});
    EXPECT_EQ(invalid.code, kExitFailure);
    EXPECT_NE(invalid.err.find("layers[0]"), std::string::npos) << invalid.err;
    EXPECT_EQ(Cli({"validate", (directory / "missing.json").string()}).code, kExitFailure);

    const auto raster = Write(directory / "raster.json",
                              R"({"schema_version": 1,
                                  "source": {"type": "raster", "path": "nowhere.tif"}})");
    EXPECT_EQ(Cli({"validate", raster.string()}).code, kExitFailure);
}

TEST(Cli, GeneratesAConfigsOutputsIntoTheChosenFolder) {
    const auto directory = Directory("generate");
    const auto config = Write(directory / "terrain.json", kConfig);
    const Result beside = Cli({config.string(), "--log-level", "off", "--threads", "2"});
    ASSERT_EQ(beside.code, kExitSuccess) << beside.err;
    EXPECT_NE(beside.out.find("Generated a 65x49 terrain in "), std::string::npos);
    EXPECT_NE(beside.out.find("Wrote 2 files to "), std::string::npos) << beside.out;
    const auto heights = terraingen::ReadGeoTiff(directory / "heights.tif").raster;
    EXPECT_EQ(heights.Width(), 65u);
    EXPECT_TRUE(std::filesystem::exists(directory / "craters.csv"));

    const auto elsewhere = Directory("generate_out");
    const Result moved = Cli({config.string(), "--output-dir", elsewhere.string()});
    ASSERT_EQ(moved.code, kExitSuccess) << moved.err;
    EXPECT_TRUE(std::filesystem::exists(elsewhere / "heights.tif"));
    const auto other = terraingen::ReadGeoTiff(elsewhere / "heights.tif").raster;
    const auto otherValues = other.Values();
    const auto heightValues = heights.Values();
    EXPECT_TRUE(std::equal(otherValues.begin(), otherValues.end(), heightValues.begin(),
                           heightValues.end()));
}

TEST(Cli, ReportsConfigAndOutputErrorsWhenGenerating) {
    const auto directory = Directory("generate_errors");
    const auto bad = Write(directory / "bad.json", R"({"schema_version": 99})");
    const Result invalid = Cli({bad.string()});
    EXPECT_EQ(invalid.code, kExitFailure);
    EXPECT_NE(invalid.err.find("bad.json: "), std::string::npos) << invalid.err;

    const auto config = Write(directory / "terrain.json", kConfig);
    const auto blocked = directory / "blocked";
    Write(blocked, "a file, not a folder");
    const Result unwritable = Cli({config.string(), "--output-dir", blocked.string()});
    EXPECT_EQ(unwritable.code, kExitFailure);
    EXPECT_EQ(unwritable.err.rfind("terraingen: ", 0), 0u) << unwritable.err;
}
