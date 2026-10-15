#include "common/example.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

namespace ex = terraingen::examples;

/// An example that makes a small ramp, writes one file and checks a flag the test sets.
class Ramp : public ex::Example {
  public:
    using Example::Example;

    void Generate() override {
        terraingen::Raster<float> heights(4, 3);
        heights.SetTransform(terraingen::GeoTransform::NorthUp(0.0, 3.0, 1.0));
        for (std::size_t row = 0; row < 3; ++row) {
            for (std::size_t col = 0; col < 4; ++col) {
                heights.At(col, row) = static_cast<float>(col);
            }
        }
        Out() << "ramp " << Args().positional.size() << "\n";
        Show("ramp", heights);
        Show("ramp again", heights, &heights);
    }

    void Save(const std::filesystem::path& dir) override {
        std::ofstream(dir / "ramp.txt") << "ramp\n";
    }

    void Test() override {
        Check(Views().size() == 2, "two views");
        Check(Args().positional.empty() || Args().positional.front() != "fail", "not told to fail");
    }
};

/// An example whose generation throws.
class Broken : public ex::Example {
  public:
    using Example::Example;
    void Generate() override {
        throw std::runtime_error("broken on purpose");
    }
    void Test() override {}
};

template <typename T> ex::ExampleInfo Info(const std::string& file, const std::string& about) {
    return ex::MakeInfo(file, about, [](const ex::ExampleArgs& args, std::ostream& out) {
        return std::unique_ptr<ex::Example>(std::make_unique<T>(args, out));
    });
}

const bool kRegistered = ex::RegisterExample(Info<Ramp>("dir/example_tiles_ramp.cpp", "A ramp")) &&
                         ex::RegisterExample(Info<Ramp>("example_terrain_ramp.cpp", "Ramp")) &&
                         ex::RegisterExample(Info<Broken>("example_zzz_broken.cpp", "Broken")) &&
                         ex::RegisterExample(Info<Ramp>("example_craters_ramp.cpp", "Ramp"));

std::filesystem::path Directory(const std::string& name) {
    const std::filesystem::path directory =
        std::filesystem::path(::testing::TempDir()) / ("terraingen_examples_" + name);
    std::filesystem::remove_all(directory);
    return directory;
}

struct Result {
    int code;
    std::string out;
    std::string err;
};

Result RunMain(const std::vector<std::string>& args, const ex::WindowRunner& window = nullptr) {
    std::ostringstream out;
    std::ostringstream err;
    const int code = ex::Main(args, out, err, window);
    return {code, out.str(), err.str()};
}

}  // namespace

TEST(ExampleHarness, NamesExamplesAfterTheirFiles) {
    ASSERT_TRUE(kRegistered);
    const ex::ExampleInfo info = Info<Ramp>("/a/b/example_craters_simple_rims.cpp", "About");
    EXPECT_EQ(info.name, "craters_simple_rims");
    EXPECT_EQ(info.category, "craters");
    EXPECT_EQ(info.Program(), "example_craters_simple_rims");
    EXPECT_EQ(info.description, "About");
    for (const char* bad : {"craters_simple.cpp", "example_craters.cpp", "example_craters_.cpp"}) {
        EXPECT_THROW(Info<Ramp>(bad, ""), std::invalid_argument) << bad;
    }
}

TEST(ExampleHarness, RefusesTwoExamplesOfOneName) {
    EXPECT_THROW(ex::RegisterExample(Info<Ramp>("example_tiles_ramp.cpp", "")),
                 std::invalid_argument);
}

TEST(ExampleHarness, OrdersByCategoryThenName) {
    std::vector<std::string> names;
    for (const ex::ExampleInfo* info : ex::Examples()) {
        names.push_back(info->name);
    }
    EXPECT_EQ(names, (std::vector<std::string>{"terrain_ramp", "craters_ramp", "tiles_ramp",
                                               "zzz_broken"}));
}

TEST(ExampleHarness, FindsExamplesWithOrWithoutThePrefix) {
    ASSERT_NE(ex::FindExample("tiles_ramp"), nullptr);
    EXPECT_EQ(ex::FindExample("example_tiles_ramp"), ex::FindExample("tiles_ramp"));
    EXPECT_EQ(ex::FindExample("tiles"), nullptr);
    EXPECT_TRUE(std::filesystem::exists(ex::ExamplesDir() / "config"));
}

TEST(ExampleHarness, ExampleShowsViewsAndChecks) {
    std::ostringstream out;
    Ramp ramp({}, out);
    ramp.Generate();
    ASSERT_EQ(ramp.Views().size(), 2u);
    EXPECT_EQ(ramp.Views()[0].name, "ramp");
    EXPECT_TRUE(ramp.Views()[0].albedo.Empty());
    EXPECT_FALSE(ramp.Views()[1].albedo.Empty());
    EXPECT_EQ(ramp.Views()[1].heights.Width(), 4u);
    EXPECT_EQ(out.str(), "ramp 0\n");
    EXPECT_NO_THROW(ramp.Test());

    ex::ExampleArgs args;
    args.positional = {"fail"};
    Ramp failing(args, out);
    failing.Generate();
    try {
        failing.Test();
        FAIL() << "the check should fail";
    } catch (const ex::CheckFailed& error) {
        EXPECT_EQ(std::string(error.what()), "Check failed: not told to fail");
    }
}

TEST(ExampleHarness, MainListsAndHelps) {
    const Result list = RunMain({"--list"});
    EXPECT_EQ(list.code, ex::kExitSuccess);
    EXPECT_EQ(list.out.rfind("terrain:\n  terrain_ramp", 0), 0u) << list.out;
    EXPECT_NE(list.out.find("\ntiles:\n  tiles_ramp"), std::string::npos) << list.out;
    EXPECT_NE(list.out.find("A ramp"), std::string::npos);

    const Result help = RunMain({"-h"});
    EXPECT_EQ(help.code, ex::kExitSuccess);
    EXPECT_EQ(help.out.rfind("Usage: terraingen-examples", 0), 0u);
}

TEST(ExampleHarness, MainRejectsBadArguments) {
    for (const std::vector<std::string>& args :
         std::vector<std::vector<std::string>>{{"--bogus"},
                                               {"tiles_ramp", "--output-dir"},
                                               {"--viewer", "gl"},
                                               {"nothing_here"},
                                               {"--viewer", "null"},
                                               {"tiles_ramp", "--viewer", "window"}}) {
        const Result result = RunMain(args);
        EXPECT_EQ(result.code, ex::kExitUsage) << args.front();
        EXPECT_FALSE(result.err.empty());
    }
}

TEST(ExampleHarness, MainRunsWithoutAViewerSavingAndChecking) {
    const std::filesystem::path dir = Directory("main");
    const Result result =
        RunMain({"example_tiles_ramp", "extra", "--output-dir", dir.string(), "--test"});
    EXPECT_EQ(result.code, ex::kExitSuccess) << result.err;
    EXPECT_EQ(result.out, "ramp 1\nexample_tiles_ramp: checks passed\n");
    EXPECT_TRUE(std::filesystem::exists(dir / "ramp.txt"));

    const std::filesystem::path unsaved = Directory("unsaved");
    EXPECT_EQ(RunMain({"tiles_ramp", "--no-save", "--output-dir", unsaved.string()}).code,
              ex::kExitSuccess);
    EXPECT_FALSE(std::filesystem::exists(unsaved));

    const Result failed = RunMain({"tiles_ramp", "fail", "--no-save", "--test"});
    EXPECT_EQ(failed.code, ex::kExitFailure);
    EXPECT_EQ(failed.err, "example_tiles_ramp: Check failed: not told to fail\n");

    const Result broken = RunMain({"zzz_broken"});
    EXPECT_EQ(broken.code, ex::kExitFailure);
    EXPECT_EQ(broken.err, "example_zzz_broken: broken on purpose\n");
}

TEST(ExampleHarness, MainOpensTheWindowWhenGivenOne) {
    const ex::ExampleInfo* opened = nullptr;
    ex::ExampleArgs seen;
    bool hidden = false;
    const ex::WindowRunner window = [&](const ex::ExampleInfo* info, const ex::ExampleArgs& args,
                                        bool headless) {
        opened = info;
        seen = args;
        hidden = headless;
        return 7;
    };
    EXPECT_EQ(RunMain({"tiles_ramp", "a", "--headless", "--test"}, window).code, 7);
    EXPECT_EQ(opened, ex::FindExample("tiles_ramp"));
    EXPECT_EQ(seen.positional, std::vector<std::string>{"a"});
    EXPECT_TRUE(seen.test);
    EXPECT_TRUE(hidden);

    EXPECT_EQ(RunMain({}, window).code, 7);
    EXPECT_EQ(opened, nullptr);
    EXPECT_FALSE(hidden);

    const std::filesystem::path dir = Directory("null");
    EXPECT_EQ(
        RunMain({"tiles_ramp", "--viewer", "null", "--output-dir", dir.string()}, window).code,
        ex::kExitSuccess);
    EXPECT_TRUE(std::filesystem::exists(dir / "ramp.txt"));
}

TEST(ExampleHarness, StandaloneNeedsExactlyOneExample) {
    std::ostringstream out;
    std::ostringstream err;
    EXPECT_EQ(ex::StandaloneMain({}, out, err), ex::kExitFailure);
    EXPECT_NE(err.str().find("exactly one example, not 4"), std::string::npos) << err.str();
}
