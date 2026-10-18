#include "terraingen/layers/density_map.h"

#include "terraingen/distribution/piecewise_uniform.h"
#include "terraingen/distribution/power_law.h"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <memory>
#include <numbers>
#include <stdexcept>

using terraingen::ConstantField;
using terraingen::DensityKeepShare;
using terraingen::DensityMap;
using terraingen::DensityMeasure;
using terraingen::DensityScale;
using terraingen::PowerLawSfd;

namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

/// A field that is `west` for negative x and `east` otherwise.
class Halves final : public terraingen::SurfaceField {
  public:
    Halves(double west, double east) : west_(west), east_(east) {}
    double ValueAt(double x, double /*y*/) const override {
        return x < 0.0 ? west_ : east_;
    }
    double Max() const override {
        return std::fmax(west_, east_);
    }

  private:
    double west_;
    double east_;
};

DensityMap Map(std::shared_ptr<const terraingen::SurfaceField> field, DensityMeasure measure,
               double minDiameter = 0.0,
               double maxDiameter = std::numeric_limits<double>::infinity()) {
    DensityMap map;
    map.field = std::move(field);
    map.measure = measure;
    map.minDiameter = minDiameter;
    map.maxDiameter = maxDiameter;
    return map;
}

}  // namespace

TEST(MapMeasure, CountsAndCoversAPowerLaw) {
    const PowerLawSfd law(0.01, 2.5);
    const auto one = std::make_shared<ConstantField>(1.0);
    EXPECT_EQ(MapMeasure(Map(one, DensityMeasure::Scale), law), 1.0);
    EXPECT_DOUBLE_EQ(MapMeasure(Map(one, DensityMeasure::Count, 4.0), law),
                     0.01 * std::pow(4.0, -2.5));
    EXPECT_DOUBLE_EQ(MapMeasure(Map(one, DensityMeasure::Count, 4.0, 9.0), law),
                     0.01 * (std::pow(4.0, -2.5) - std::pow(9.0, -2.5)));
    const double covered = std::numbers::pi * 0.01 * 2.5 / 4.0 * 2.0 * (0.5 - 1.0 / 3.0);
    EXPECT_NEAR(MapMeasure(Map(one, DensityMeasure::ArealFraction, 4.0, 9.0), law), covered,
                1e-9 * covered)
        << "pi c e / 4 times the integral of D^-1.5 from 4 to 9 m";
    EXPECT_THROW(MapMeasure(Map(one, DensityMeasure::ArealFraction, 4.0), law), std::domain_error);
}

TEST(DensityScale, ScalesTheDistributionToTheMap) {
    const PowerLawSfd law(0.01, 2.0);
    DensityMap map = Map(std::make_shared<Halves>(0.5, 3.0), DensityMeasure::Count, 1.0);
    map.areaUnit = 1e6;
    const DensityScale scale(map, PowerLawSfd(1e4, 2.0));
    EXPECT_DOUBLE_EQ(scale.At(-1.0, 0.0), 0.5 / 1e6 / 1e4);
    EXPECT_DOUBLE_EQ(scale.Max(), 1.0) << "never below 1, the scale where the map has no value";
    const DensityScale denser(Map(std::make_shared<Halves>(0.5, 3.0), DensityMeasure::Scale), law);
    EXPECT_DOUBLE_EQ(denser.At(-1.0, 0.0), 0.5);
    EXPECT_DOUBLE_EQ(denser.At(1.0, 0.0), 3.0);
    EXPECT_DOUBLE_EQ(denser.Max(), 3.0);
    const DensityScale missing(Map(std::make_shared<ConstantField>(kNaN), DensityMeasure::Scale),
                               law);
    EXPECT_DOUBLE_EQ(missing.At(0.0, 0.0), 1.0) << "the density as given where the map is empty";
    const DensityScale negative(Map(std::make_shared<Halves>(-1.0, 2.0), DensityMeasure::Scale),
                                law);
    EXPECT_DOUBLE_EQ(negative.At(-1.0, 0.0), 0.0);
    const DensityKeepShare share(std::make_shared<DensityScale>(denser));
    EXPECT_DOUBLE_EQ(share.ValueAt(-1.0, 0.0), 0.5 / 3.0);
    EXPECT_DOUBLE_EQ(share.ValueAt(1.0, 0.0), 1.0);
    EXPECT_DOUBLE_EQ(share.Max(), 1.0);
}

TEST(ValidateDensityMap, RejectsUnusableMaps) {
    const auto one = std::make_shared<ConstantField>(1.0);
    EXPECT_NO_THROW(ValidateDensityMap(Map(one, DensityMeasure::Scale)));
    EXPECT_THROW(ValidateDensityMap(Map(nullptr, DensityMeasure::Scale)), std::invalid_argument);
    EXPECT_THROW(ValidateDensityMap(Map(one, DensityMeasure::Count)), std::invalid_argument);
    EXPECT_THROW(ValidateDensityMap(Map(one, DensityMeasure::Count, 5.0, 5.0)),
                 std::invalid_argument);
    DensityMap unit = Map(one, DensityMeasure::Count, 1.0);
    unit.areaUnit = 0.0;
    EXPECT_THROW(ValidateDensityMap(unit), std::invalid_argument);
    EXPECT_THROW(DensityScale(Map(one, DensityMeasure::Count, 5.0, 6.0),
                              terraingen::PiecewiseUniformSfd({{1.0, 2.0, 1.0}})),
                 std::invalid_argument)
        << "measures none of the distribution";
    EXPECT_THROW(DensityKeepShare(nullptr), std::invalid_argument);
}
