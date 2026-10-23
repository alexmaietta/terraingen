#include "terraingen/terraingen.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <vector>

using terraingen::CraterApplyOptions;
using terraingen::CraterDatum;
using terraingen::FassettThomsonProfile;
using terraingen::HeightSampler;
using terraingen::Plane;
using terraingen::PlaneFit;
using terraingen::PreparedCrater;

namespace {

const terraingen::NoDegradation kFresh(terraingen::LunarFreshCraterMorphometry);

}  // namespace

TEST(Plane, IsLinearInTheOffset) {
    const Plane plane{2.0, 0.5, -0.25};
    EXPECT_EQ(plane.At(0.0, 0.0), 2.0);
    EXPECT_EQ(plane.At(4.0, 8.0), 2.0 + 2.0 - 2.0);
}

TEST(PlaneFit, FitsAPlaneAndFallsBackToTheMean) {
    PlaneFit fit;
    EXPECT_TRUE(fit.Empty());
    for (const double dx : {-1.0, 0.0, 1.0}) {
        for (const double dy : {-1.0, 0.0, 1.0}) {
            fit.Add(dx, dy, 3.0 + 2.0 * dx - dy);
        }
    }
    EXPECT_FALSE(fit.Empty());
    EXPECT_DOUBLE_EQ(fit.Mean(), 3.0);
    const Plane plane = fit.Solve();
    EXPECT_NEAR(plane.a, 3.0, 1e-12);
    EXPECT_NEAR(plane.b, 2.0, 1e-12);
    EXPECT_NEAR(plane.c, -1.0, 1e-12);
    PlaneFit line;
    line.Add(0.0, 0.0, 1.0);
    line.Add(1.0, 0.0, 3.0);
    line.Add(2.0, 0.0, 5.0);
    EXPECT_EQ(line.Solve(), (Plane{3.0, 0.0, 0.0})) << "points on a line fix no plane";
}

TEST(FitCraterDatum, FitsTheGroundOnALatticeAroundTheCrater) {
    const HeightSampler slope = [](double x, double y) {
        return static_cast<float>(10.0 + 0.5 * x - 0.25 * y);
    };
    const terraingen::Crater crater{100.0, -40.0, 20.0, 0.0, 1u};
    CraterApplyOptions options;
    options.datum = CraterDatum::Plane;
    const auto plane = terraingen::FitCraterDatum(crater, options, slope);
    ASSERT_TRUE(plane.has_value());
    EXPECT_NEAR(plane->a, 10.0 + 50.0 + 10.0, 1e-4) << "height under the centre";
    EXPECT_NEAR(plane->b, 0.5, 1e-6);
    EXPECT_NEAR(plane->c, -0.25, 1e-6);
    options.datum = CraterDatum::MeanHeight;
    EXPECT_NEAR(terraingen::FitCraterDatum(crater, options, slope)->a, 70.0, 1e-4);
    options.datum = CraterDatum::CenterHeight;
    EXPECT_FLOAT_EQ(static_cast<float>(terraingen::FitCraterDatum(crater, options, slope)->a),
                    70.0f);
    const HeightSampler none = [](double, double) { return std::nanf(""); };
    EXPECT_FALSE(terraingen::FitCraterDatum(crater, options, none).has_value());
    options.datum = CraterDatum::Plane;
    EXPECT_FALSE(terraingen::FitCraterDatum(crater, options, none).has_value());
    options.datum = CraterDatum::None;
    EXPECT_FALSE(terraingen::FitCraterDatum(crater, options, slope).has_value());
}

TEST(DatumMargin, CoversTheFitFootprintAndReachOfTheLargestCrater) {
    const FassettThomsonProfile profile;
    EXPECT_EQ(terraingen::DatumMargin({}, profile, 100.0), 0.0);
    CraterApplyOptions options;
    options.datum = CraterDatum::Plane;
    options.datumFitRadii = 1.5;
    EXPECT_DOUBLE_EQ(terraingen::DatumMargin(options, profile, 100.0),
                     (1.5 + profile.ReachRadii()) * 50.0);
}

TEST(ForEachDatumPoint, VisitsTheLatticeInsideTheFootprintRowByRow) {
    std::vector<std::size_t> points;
    terraingen::ForEachDatumPoint(7.0, [&](std::size_t point, double dx, double dy) {
        EXPECT_LT(dx * dx + dy * dy, 49.0);
        EXPECT_EQ(dx, terraingen::DatumLatticeOffset(static_cast<int>(point % 7), 7.0));
        EXPECT_EQ(dy, terraingen::DatumLatticeOffset(static_cast<int>(point / 7), 7.0));
        points.push_back(point);
    });
    EXPECT_EQ(points.size(), 37u) << "the corners lie outside";
    EXPECT_TRUE(std::is_sorted(points.begin(), points.end()));
    EXPECT_EQ(points.front(), 2u);
}
