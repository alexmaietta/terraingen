// SPDX-License-Identifier: BSD-3-Clause

#include "config/terrain.h"
#include "configs.h"
#include "terraingen/config/validate.h"
#include "terraingen/util/option_fields.h"

#include <gtest/gtest.h>

#include <limits>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

namespace config = terraingen::config;
using terraingen::Above;
using terraingen::AboveUpTo;
using terraingen::AtLeast;
using terraingen::Between;
using terraingen::CheckLength;
using terraingen::CheckRange;
using terraingen::CheckValueRanges;
using terraingen::ConfigError;
using terraingen::ConfigSpec;
using terraingen::DescribeLength;
using terraingen::DescribeRange;
using terraingen::FieldHelp;
using terraingen::FieldRange;
using terraingen::FromBelow;
using terraingen::Inside;
using terraingen::Length;
using terraingen::LengthEither;
using terraingen::MinLength;

namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
constexpr double kInfinity = std::numeric_limits<double>::infinity();

/// The message `check` throws as a ConfigError or a FieldRangeError, or "valid".
template <typename Check> std::string Message(Check check) {
    try {
        check();
    } catch (const ConfigError& error) {
        return error.what();
    } catch (const terraingen::FieldRangeError& error) {
        return error.what();
    }
    return "valid";
}

/// The path of the ConfigError `CheckConfigRanges` throws for `config`, or "valid".
std::string RangeErrorPath(const ConfigSpec& config) {
    try {
        config::CheckConfigRanges(config);
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
            const std::string name(terraingen::SpecName<Ts>::kName);
            const std::string message = Message([&] { CheckValueRanges(value, "x", {}, {}); });
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
    EXPECT_EQ(DescribeRange(Above(0.0)), "positive");
    EXPECT_EQ(DescribeRange(AtLeast(0.0), "m"), "not negative");
    EXPECT_EQ(DescribeRange(AtLeast(16.0), "px"), "at least 16 px");
    EXPECT_EQ(DescribeRange(Above(1.0)), "above 1");
    EXPECT_EQ(DescribeRange(Between(1.0, 4096.0)), "from 1 to 4096");
    EXPECT_EQ(DescribeRange(Between(0.0, 180.0), "deg"), "from 0 to 180 deg");
    EXPECT_EQ(DescribeRange(FromBelow(0.0, 1.0)), "from 0 to below 1");
    EXPECT_EQ(DescribeRange(AboveUpTo(0.0, 0.5)), "above 0 and at most 0.5");
    EXPECT_EQ(DescribeRange(Inside(0.0, 1.0)), "above 0 and below 1");
    EXPECT_EQ(DescribeRange(FieldRange{std::nullopt, 2.0, false, true}), "below 2");
    EXPECT_EQ(DescribeRange(FieldRange{std::nullopt, 2.0}), "at most 2");
    EXPECT_EQ(DescribeRange({}), "any number");
}

TEST(CheckRange, PassesValuesInsideTheRange) {
    EXPECT_NO_THROW(CheckRange(1.0, Above(0.0), "", "a"));
    EXPECT_NO_THROW(CheckRange(0.0, AtLeast(0.0), "", "a"));
    EXPECT_NO_THROW(CheckRange(180.0, Between(0.0, 180.0), "deg", "a"));
    EXPECT_NO_THROW(CheckRange(-1e300, {}, "", "a"))
        << "an unbounded field takes any finite number";
}

TEST(CheckRange, ReportsValuesOutsideTheRangeAtTheirPath) {
    EXPECT_EQ(Message([] { CheckRange(kNaN, {}, "", "a"); }), "a: must be finite");
    EXPECT_EQ(Message([] { CheckRange(-kInfinity, {}, "", "a"); }), "a: must be finite");
    EXPECT_EQ(Message([] { CheckRange(0.0, Above(0.0), "m", "a.b"); }),
              "a.b: must be positive, got 0");
    EXPECT_EQ(Message([] { CheckRange(-1.0, AtLeast(0.0), "", "c"); }),
              "c: must not be negative, got -1");
    EXPECT_EQ(Message([] { CheckRange(200.0, Between(0.0, 180.0), "deg", "d"); }),
              "d: must be from 0 to 180 deg, got 200");
    EXPECT_EQ(Message([] { CheckRange(1.0, FromBelow(0.0, 1.0), "", "e"); }),
              "e: must be from 0 to below 1, got 1");
    EXPECT_EQ(Message([] { CheckRange(0.0, Inside(0.0, 1.0), "", "f"); }),
              "f: must be above 0 and below 1, got 0");
}

TEST(CheckRange, ThrowsAFieldRangeErrorWithItsPathAndRule) {
    try {
        CheckRange(0.0, Above(0.0), "m", "a.b");
        FAIL() << "expected a FieldRangeError";
    } catch (const terraingen::FieldRangeError& error) {
        EXPECT_EQ(error.Path(), "a.b");
        EXPECT_EQ(error.Rule(), "must be positive, got 0");
    }
}

TEST(CheckRange, NeedsFiniteValuesInABoundedRange) {
    EXPECT_EQ(Message([] { CheckRange(kInfinity, Above(0.0), "", "a"); }),
              "a: must be positive, got inf");
    EXPECT_EQ(Message([] { CheckRange(kNaN, AtLeast(0.0), "", "b"); }),
              "b: must not be negative, got nan");
}

TEST(CheckValueRanges, WalksOptionalsListsAndStructs) {
    std::optional<double> unset;
    EXPECT_NO_THROW(CheckValueRanges(unset, "a", {}, Above(0.0)));
    std::vector<double> radii{1.0, -2.0};
    EXPECT_EQ(Message([&] { CheckValueRanges(radii, "radii", "m", Above(0.0)); }),
              "radii[1]: must be positive, got -2");
    terraingen::HeightmapOutput output;
    output.area.pixelSize = -1.0;
    EXPECT_EQ(Message([&] { CheckValueRanges(output, "output", {}, {}); }),
              "output.pixel_size_m: must be positive, got -1");
    output.area.pixelSize = 1.0;
    output.area.extentM = terraingen::ExtentSpec{{{0.0, 0.0}}, {{4.0, -4.0}}, {}, {}};
    EXPECT_EQ(Message([&] { CheckValueRanges(output, "output", {}, {}); }),
              "output.extent_m.size[1]: must be positive, got -4");
}

TEST(CheckRanges, NamesTheFieldOfAnOptionsStruct) {
    terraingen::SandFillOptions fill;
    fill.reach = 0.0;
    EXPECT_EQ(Message([&] { terraingen::CheckRanges(fill); }), "reach_m: must be positive, got 0");
    EXPECT_THROW(terraingen::CheckRanges(fill), std::invalid_argument);
}

TEST(CheckConfigRanges, ReportsTheFirstNumberOutOfRangeByItsPath) {
    ConfigSpec config = terraingen::testing::Small();
    EXPECT_EQ(RangeErrorPath(config), "valid");
    terraingen::testing::Region(config).pixelSize = 0.0;
    EXPECT_EQ(RangeErrorPath(config), "outputs[0].pixel_size_m");
    config = terraingen::testing::Small();
    terraingen::testing::Craters(config).options.generation.spacing.diameterRatio = 0.5;
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
    EXPECT_EQ(Message([&] { CheckValueRanges(materials, "materials", {}, {}); }), "valid");
}

TEST(DescribeLength, SaysHowManyItemsAListHolds) {
    EXPECT_EQ(DescribeLength({}), "");
    EXPECT_EQ(DescribeLength(Length(3)), "exactly 3 items");
    EXPECT_EQ(DescribeLength(Length(1, 8)), "1 to 8 items");
    EXPECT_EQ(DescribeLength(MinLength(1)), "at least 1 item");
    EXPECT_EQ(DescribeLength(LengthEither(1, 3)), "1 or 3 items");
}

TEST(CheckLength, ReportsListsOfAnotherLengthAtTheirPath) {
    EXPECT_NO_THROW(CheckLength(5, {}, "a"));
    EXPECT_NO_THROW(CheckLength(3, Length(3), "a"));
    EXPECT_NO_THROW(CheckLength(0, LengthEither(0, 2), "a"));
    EXPECT_EQ(Message([] { CheckLength(2, Length(3), "a"); }),
              "a: must hold exactly 3 items, got 2");
    EXPECT_EQ(Message([] { CheckLength(9, Length(1, 8), "b"); }),
              "b: must hold 1 to 8 items, got 9");
    EXPECT_EQ(Message([] { CheckLength(0, MinLength(1), "c"); }),
              "c: must hold at least 1 item, got 0");
    EXPECT_EQ(Message([] { CheckLength(2, LengthEither(1, 3), "d"); }),
              "d: must hold 1 or 3 items, got 2");
}

TEST(CheckValueRanges, ChecksListLengthsAndRequiredOptionals) {
    std::vector<double> radii{1.0, 2.0};
    EXPECT_EQ(Message([&] { CheckValueRanges(radii, "radii", "m", Above(0.0).Length(3)); }),
              "radii: must hold exactly 3 items, got 2");
    std::optional<double> unset;
    EXPECT_EQ(Message([&] { CheckValueRanges(unset, "a", {}, terraingen::Required()); }),
              "a: required");
    ConfigSpec config;
    config.body = terraingen::EllipsoidSurfaceSpec{{3.0, 2.0}};
    EXPECT_EQ(RangeErrorPath(config), "body.radii_m");
}

TEST(FieldHelp, AppendsWhatTheRangeSaysToTheDescription) {
    EXPECT_EQ(FieldHelp("Width.", "m", {}, false), "Width.");
    EXPECT_EQ(FieldHelp("Width.", "m", Above(0.0), false), "Width. Must be positive.");
    EXPECT_EQ(FieldHelp("Share.", "", terraingen::UnitInterval(), false),
              "Share. Must be from 0 to 1.");
    EXPECT_EQ(FieldHelp("Depth.", "m", AtLeast(0.0), false), "Depth. Must not be negative.");
    EXPECT_EQ(FieldHelp("Semi-axes.", "m", Above(0.0).Length(3).Required(), true),
              "Semi-axes. Holds exactly 3 items. Each must be positive. Required.");
}
