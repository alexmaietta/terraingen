#include "app.h"

#include <gtest/gtest.h>

#include <stdexcept>
#include <vector>

using terraingen::viewer::AppOptions;
using terraingen::viewer::ParseAppOptions;

namespace {

AppOptions Parse(std::vector<const char*> args) {
    args.insert(args.begin(), "terraingen-viewer");
    return ParseAppOptions(static_cast<int>(args.size()), args.data());
}

}  // namespace

TEST(ParseAppOptions, DefaultsWithoutArguments) {
    const AppOptions options = Parse({});
    EXPECT_EQ(options.maxFrames, -1);
    EXPECT_TRUE(options.brdf.empty());
    EXPECT_TRUE(options.detailAlbedo.empty());
    EXPECT_TRUE(options.detailNormal.empty());
    EXPECT_EQ(options.logLevel, terraingen::LogLevel::Info);
    EXPECT_FALSE(options.validation);
    EXPECT_FALSE(options.hidden);
}

TEST(ParseAppOptions, ReadsEveryOption) {
    const AppOptions options =
        Parse({"--frames", "30", "--brdf", "hapke", "--detail-albedo", "rock_diff.png",
               "--detail-normal", "rock_nor_gl.exr", "--log-level", "debug", "--hidden"});
    EXPECT_EQ(options.maxFrames, 30);
    EXPECT_EQ(options.brdf, "hapke");
    EXPECT_EQ(options.detailAlbedo, "rock_diff.png");
    EXPECT_EQ(options.detailNormal, "rock_nor_gl.exr");
    EXPECT_EQ(options.logLevel, terraingen::LogLevel::Debug);
    EXPECT_TRUE(options.hidden);
}

TEST(ParseAppOptions, RejectsUnknownFlagsMissingValuesAndBadNumbers) {
    EXPECT_THROW(Parse({"--brightness", "2"}), std::invalid_argument);
    EXPECT_THROW(Parse({"--brdf"}), std::invalid_argument);
    EXPECT_THROW(Parse({"--frames", "ten"}), std::invalid_argument);
    EXPECT_THROW(Parse({"--frames", "10x"}), std::invalid_argument);
    EXPECT_THROW(Parse({"--frames", ""}), std::invalid_argument);
    EXPECT_THROW(Parse({"--frames", "99999999999999999999999"}), std::invalid_argument);
    EXPECT_THROW(Parse({"--log-level", "loud"}), std::invalid_argument);
    EXPECT_THROW(Parse({"--log-level"}), std::invalid_argument);
}

TEST(AppUsage, ListsEveryOption) {
    const std::string usage = terraingen::viewer::AppUsage();
    for (const char* flag : {"--brdf", "--detail-albedo", "--detail-normal", "--frames",
                             "--log-level", "--validation", "--hidden", "--help"}) {
        EXPECT_NE(usage.find(flag), std::string::npos) << flag;
    }
}

TEST(ParseAppOptions, EnablesValidation) {
    EXPECT_TRUE(Parse({"--validation"}).validation);
}
