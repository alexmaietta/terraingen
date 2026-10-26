// SPDX-License-Identifier: BSD-3-Clause

#include "config/build_steps.h"
#include "config/fields/fields.h"
#include "config/fields/ranges.h"
#include "configs.h"
#include "terraingen/config/validate.h"

#include <gtest/gtest.h>

#include <limits>
#include <optional>
#include <set>
#include <string>
#include <variant>
#include <vector>

namespace config = terraingen::config;
using config::AboveUpTo;
using config::AtLeast;
using config::Between;
using config::FieldRange;
using config::FromBelow;
using config::Inside;
using terraingen::ConfigError;
using terraingen::ConfigSpec;

namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
constexpr double kInfinity = std::numeric_limits<double>::infinity();

/// The message `check` throws as a ConfigError, or "valid".
template <typename Check> std::string Message(Check check) {
    try {
        check();
    } catch (const ConfigError& error) {
        return error.what();
    }
    return "valid";
}

/// The path of the ConfigError `CheckConfigRanges` throws for `config`, or "valid".
std::string RangeErrorPath(const ConfigSpec& config) {
    try {
        terraingen::config::CheckConfigRanges(config);
    } catch (const ConfigError& error) {
        return error.Path();
    }
    return "valid";
}

/// Checks every alternative of a variant, default-constructed, against its fields' ranges: valid
/// unless named in `needData`, whose defaults leave a required value or list empty.
template <typename... Ts>
void CheckDefaults(const std::variant<Ts...>* /*tag*/, const std::set<std::string>& needData) {
    (
        [&] {
            std::variant<Ts...> value = Ts{};
            const std::string name(config::SpecName<Ts>::kName);
            const std::string message = Message([&] { config::CheckRanges(value, "x", {}, {}); });
            if (needData.count(name) != 0) {
                EXPECT_NE(message, "valid") << name;
            } else {
                EXPECT_EQ(message, "valid") << name;
            }
        }(),
        ...);
}

template <typename Variant> void CheckDefaults(const std::set<std::string>& needData = {}) {
    CheckDefaults(static_cast<const Variant*>(nullptr), needData);
}

}  // namespace

TEST(DescribeRange, SaysWhatEachKindOfRangeAllows) {
    EXPECT_EQ(config::DescribeRange(config::Above(0.0)), "positive");
    EXPECT_EQ(config::DescribeRange(AtLeast(0.0), "m"), "not negative");
    EXPECT_EQ(config::DescribeRange(AtLeast(16.0), "px"), "at least 16 px");
    EXPECT_EQ(config::DescribeRange(config::Above(1.0)), "above 1");
    EXPECT_EQ(config::DescribeRange(Between(1.0, 4096.0)), "from 1 to 4096");
    EXPECT_EQ(config::DescribeRange(Between(0.0, 180.0), "deg"), "from 0 to 180 deg");
    EXPECT_EQ(config::DescribeRange(FromBelow(0.0, 1.0)), "from 0 to below 1");
    EXPECT_EQ(config::DescribeRange(AboveUpTo(0.0, 0.5)), "above 0 and at most 0.5");
    EXPECT_EQ(config::DescribeRange(Inside(0.0, 1.0)), "above 0 and below 1");
    EXPECT_EQ(config::DescribeRange(FieldRange{std::nullopt, 2.0, false, true}), "below 2");
    EXPECT_EQ(config::DescribeRange(FieldRange{std::nullopt, 2.0}), "at most 2");
    EXPECT_EQ(config::DescribeRange({}), "any number");
}

TEST(CheckRange, PassesValuesInsideTheRange) {
    EXPECT_NO_THROW(config::CheckRange(1.0, config::Above(0.0), "", "a"));
    EXPECT_NO_THROW(config::CheckRange(0.0, AtLeast(0.0), "", "a"));
    EXPECT_NO_THROW(config::CheckRange(180.0, Between(0.0, 180.0), "deg", "a"));
    EXPECT_NO_THROW(config::CheckRange(-1e300, {}, "", "a"))
        << "an unbounded field takes any finite number";
}

TEST(CheckRange, ReportsValuesOutsideTheRangeAtTheirPath) {
    EXPECT_EQ(Message([] { config::CheckRange(kNaN, {}, "", "a"); }), "a: must be finite");
    EXPECT_EQ(Message([] { config::CheckRange(-kInfinity, {}, "", "a"); }), "a: must be finite");
    EXPECT_EQ(Message([] { config::CheckRange(0.0, config::Above(0.0), "m", "a.b"); }),
              "a.b: must be positive, got 0");
    EXPECT_EQ(Message([] { config::CheckRange(-1.0, AtLeast(0.0), "", "c"); }),
              "c: must not be negative, got -1");
    EXPECT_EQ(Message([] { config::CheckRange(200.0, Between(0.0, 180.0), "deg", "d"); }),
              "d: must be from 0 to 180 deg, got 200");
    EXPECT_EQ(Message([] { config::CheckRange(1.0, FromBelow(0.0, 1.0), "", "e"); }),
              "e: must be from 0 to below 1, got 1");
    EXPECT_EQ(Message([] { config::CheckRange(0.0, Inside(0.0, 1.0), "", "f"); }),
              "f: must be above 0 and below 1, got 0");
}

TEST(CheckRange, NeedsFiniteValuesInABoundedRange) {
    EXPECT_EQ(Message([] { config::CheckRange(kInfinity, config::Above(0.0), "", "a"); }),
              "a: must be positive, got inf");
    EXPECT_EQ(Message([] { config::CheckRange(kNaN, AtLeast(0.0), "", "b"); }),
              "b: must not be negative, got nan");
}

TEST(CheckRanges, WalksOptionalsListsAndStructs) {
    std::optional<double> unset;
    EXPECT_NO_THROW(config::CheckRanges(unset, "a", {}, config::Above(0.0)));
    std::vector<double> radii{1.0, -2.0};
    EXPECT_EQ(Message([&] { config::CheckRanges(radii, "radii", "m", config::Above(0.0)); }),
              "radii[1]: must be positive, got -2");
    terraingen::HeightmapOutput output;
    output.area.pixelSize = -1.0;
    EXPECT_EQ(Message([&] { config::CheckRanges(output, "output", {}, {}); }),
              "output.pixel_size_m: must be positive, got -1");
    output.area.pixelSize = 1.0;
    output.area.extentM = terraingen::ExtentSpec{{0.0, 0.0}, {4.0, -4.0}, {}, {}};
    EXPECT_EQ(Message([&] { config::CheckRanges(output, "output", {}, {}); }),
              "output.extent_m.size[1]: must be positive, got -4");
}

TEST(CheckConfigRanges, ReportsTheFirstNumberOutOfRangeByItsPath) {
    ConfigSpec config = terraingen::testing::Small();
    EXPECT_EQ(RangeErrorPath(config), "valid");
    terraingen::testing::Region(config).pixelSize = 0.0;
    EXPECT_EQ(RangeErrorPath(config), "outputs[0].pixel_size_m");
    config = terraingen::testing::Small();
    terraingen::testing::Craters(config).spacing.diameterRatio = 0.5;
    EXPECT_EQ(RangeErrorPath(config), "layers[1].spacing.diameter_ratio");
    config = terraingen::testing::Small();
    terraingen::testing::Noise(config).stack[0].fractal.octaves = 0;
    EXPECT_EQ(RangeErrorPath(config), "layers[0].stack[0].octaves");
}

TEST(CheckConfigRanges, RejectsNumbersThatAreNotFiniteInFieldsWithoutARange) {
    ConfigSpec config = terraingen::testing::Small();
    terraingen::testing::Noise(config).stack[0].hurst = static_cast<float>(kNaN);
    EXPECT_EQ(RangeErrorPath(config), "layers[0].stack[0].hurst");
    config = terraingen::testing::Small();
    terraingen::ScaleLayerSpec scale;
    scale.offset = kInfinity;
    config.layers.push_back(scale);
    EXPECT_EQ(Message([&] { config::CheckConfigRanges(config); }),
              "layers[2].offset_m: must be finite");
}

TEST(CheckConfigRanges, ChecksInsideRegionsAndTheBody) {
    ConfigSpec config;
    config.body = terraingen::EllipsoidSurfaceSpec{{3.0, 0.0, 1.0}};
    EXPECT_EQ(RangeErrorPath(config), "body.radii_m[1]");
    config.body = terraingen::ShapeModelSurfaceSpec{"a.icq", terraingen::kMaxCubeFaceCells + 1};
    EXPECT_EQ(RangeErrorPath(config), "body.q");
    config = ConfigSpec{};
    terraingen::MaskedLayerSpec region;
    terraingen::RoughnessLayerSpec roughness;
    roughness.roughness.baseline = 0.0;
    region.layer = roughness;
    config.layers = {region};
    EXPECT_EQ(RangeErrorPath(config), "layers[0].layer.baseline_m");
}

TEST(CheckConfigRanges, AcceptsEveryDefaultThatNeedsNoData) {
    EXPECT_EQ(RangeErrorPath(ConfigSpec{}), "valid");
    CheckDefaults<terraingen::LayerSpec>({"secondaries"});
    CheckDefaults<terraingen::OutputSpec>();
    CheckDefaults<terraingen::SourceSpec>({"chain"});
    CheckDefaults<terraingen::DensitySpec>({"tabulated"});
    CheckDefaults<terraingen::AgesSpec>();
    CheckDefaults<terraingen::DegradationSpec>();
    CheckDefaults<terraingen::ProfileSpec>();
    CheckDefaults<terraingen::BoulderSizesSpec>({"tabulated"});
    CheckDefaults<terraingen::RegionShapeSpec>({"polygon"});
    CheckDefaults<terraingen::AlbedoSourceSpec>();
    CheckDefaults<terraingen::AlbedoLayerSpec>();
    terraingen::MaterialsSpec materials;
    EXPECT_EQ(Message([&] { config::CheckRanges(materials, "materials", {}, {}); }), "valid");
}

TEST(DescribeLength, SaysHowManyItemsAListHolds) {
    EXPECT_EQ(config::DescribeLength({}), "");
    EXPECT_EQ(config::DescribeLength(config::Length(3)), "exactly 3 items");
    EXPECT_EQ(config::DescribeLength(config::Length(1, 8)), "1 to 8 items");
    EXPECT_EQ(config::DescribeLength(config::MinLength(1)), "at least 1 item");
    EXPECT_EQ(config::DescribeLength(config::LengthEither(1, 3)), "1 or 3 items");
}

TEST(CheckLength, ReportsListsOfAnotherLengthAtTheirPath) {
    EXPECT_NO_THROW(config::CheckLength(5, {}, "a"));
    EXPECT_NO_THROW(config::CheckLength(3, config::Length(3), "a"));
    EXPECT_NO_THROW(config::CheckLength(0, config::LengthEither(0, 2), "a"));
    EXPECT_EQ(Message([] { config::CheckLength(2, config::Length(3), "a"); }),
              "a: must hold exactly 3 items, got 2");
    EXPECT_EQ(Message([] { config::CheckLength(9, config::Length(1, 8), "b"); }),
              "b: must hold 1 to 8 items, got 9");
    EXPECT_EQ(Message([] { config::CheckLength(0, config::MinLength(1), "c"); }),
              "c: must hold at least 1 item, got 0");
    EXPECT_EQ(Message([] { config::CheckLength(2, config::LengthEither(1, 3), "d"); }),
              "d: must hold 1 or 3 items, got 2");
}

TEST(CheckRanges, ChecksListLengthsAndRequiredOptionals) {
    std::vector<double> radii{1.0, 2.0};
    EXPECT_EQ(
        Message([&] { config::CheckRanges(radii, "radii", "m", config::Above(0.0).Length(3)); }),
        "radii: must hold exactly 3 items, got 2");
    std::optional<double> unset;
    EXPECT_EQ(Message([&] { config::CheckRanges(unset, "a", {}, config::Required()); }),
              "a: required");
    ConfigSpec config;
    config.body = terraingen::EllipsoidSurfaceSpec{{3.0, 2.0}};
    EXPECT_EQ(RangeErrorPath(config), "body.radii_m");
}

TEST(FieldHelp, AppendsWhatTheRangeSaysToTheDescription) {
    EXPECT_EQ(config::FieldHelp("Width.", "m", {}, false), "Width.");
    EXPECT_EQ(config::FieldHelp("Width.", "m", config::Above(0.0), false),
              "Width. Must be positive.");
    EXPECT_EQ(config::FieldHelp("Share.", "", config::UnitInterval(), false),
              "Share. Must be from 0 to 1.");
    EXPECT_EQ(config::FieldHelp("Depth.", "m", AtLeast(0.0), false),
              "Depth. Must not be negative.");
    EXPECT_EQ(config::FieldHelp("Semi-axes.", "m", config::Above(0.0).Length(3).Required(), true),
              "Semi-axes. Holds exactly 3 items. Each must be positive. Required.");
}
