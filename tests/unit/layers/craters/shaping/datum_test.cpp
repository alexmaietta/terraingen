#include "terraingen/terraingen.h"

#include <gtest/gtest.h>

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
    options.datum = CraterDatum::CentreHeight;
    EXPECT_FLOAT_EQ(static_cast<float>(terraingen::FitCraterDatum(crater, options, slope)->a),
                    70.0f);
    const HeightSampler none = [](double, double) { return std::nanf(""); };
    EXPECT_FALSE(terraingen::FitCraterDatum(crater, options, none).has_value());
    options.datum = CraterDatum::Plane;
    EXPECT_FALSE(terraingen::FitCraterDatum(crater, options, none).has_value());
    options.datum = CraterDatum::None;
    EXPECT_FALSE(terraingen::FitCraterDatum(crater, options, slope).has_value());
}

TEST(CraterReliefIndex, SumsEarlierCratersReachingAPointWhateverElseTheListHolds) {
    const FassettThomsonProfile profile;
    const terraingen::CraterState fresh = kFresh.State({0, 0, 10, 0, 1});
    const std::vector<PreparedCrater> near{{{0.0, 0.0, 10.0, 0.0, 1u}, 1.0, fresh},
                                           {{3.0, 1.0, 6.0, 0.0, 2u}, 0.5, fresh},
                                           {{1.0, -2.0, 8.0, 0.0, 3u}, 1.0, fresh}};
    const terraingen::CraterReliefIndex small(near, profile);
    EXPECT_EQ(small.ReliefBefore(1.0, 0.5, 0), 0.0);
    const double first = small.ReliefBefore(1.0, 0.5, 1);
    EXPECT_DOUBLE_EQ(first, profile.Height(std::hypot(1.0, 0.5), 10.0, fresh));
    const double two = small.ReliefBefore(1.0, 0.5, 2);
    EXPECT_DOUBLE_EQ(two, first + 0.5 * profile.Height(std::hypot(2.0, 0.5), 6.0, fresh));
    std::vector<PreparedCrater> more = near;
    more.insert(more.begin() + 1, {{500.0, 500.0, 40.0, 0.0, 9u}, 1.0, fresh});
    more.push_back({{-300.0, 0.0, 12.0, 0.0, 10u}, 1.0, fresh});
    const terraingen::CraterReliefIndex large(more, profile);
    EXPECT_EQ(large.ReliefBefore(1.0, 0.5, 4), small.ReliefBefore(1.0, 0.5, 3));
    EXPECT_EQ(large.Craters().size(), 5u);
}

TEST(DatumGround, AddsTheReliefOfEarlierCratersToTheGround) {
    const FassettThomsonProfile profile;
    const terraingen::CraterState fresh = kFresh.State({0, 0, 10, 0, 1});
    const std::vector<PreparedCrater> craters{{{0.0, 0.0, 10.0, 0.0, 1u}, 1.0, fresh},
                                              {{2.0, 0.0, 10.0, 0.0, 2u}, 1.0, fresh}};
    const terraingen::CraterReliefIndex index(craters, profile);
    const HeightSampler flat = [](double, double) { return 4.0f; };
    const HeightSampler ground = terraingen::DatumGround(flat, index, 1);
    EXPECT_FLOAT_EQ(ground(1.0, 0.0), static_cast<float>(4.0 + profile.Height(1.0, 10.0, fresh)));
    const HeightSampler none = [](double, double) { return std::nanf(""); };
    EXPECT_TRUE(std::isnan(terraingen::DatumGround(none, index, 1)(1.0, 0.0)));
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
