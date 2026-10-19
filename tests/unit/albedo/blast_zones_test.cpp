#include "terraingen/albedo/blast_zones.h"

#include "terraingen/albedo/synthetic.h"
#include "terraingen/util/bits.h"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <memory>
#include <stdexcept>

using terraingen::Albedo;
using terraingen::BlastZone;
using terraingen::BlastZoneAlbedo;
using terraingen::GeoTransform;
using terraingen::RasterGrid;

namespace {

BlastZone Round() {
    BlastZone zone;
    zone.elongation = 1.0;
    zone.raggedness = 0.0;
    return zone;
}

}  // namespace

TEST(BlastZoneAlbedo, AveragesTheMeasuredChangeOverTheZone) {
    const BlastZone zone = Round();
    const BlastZoneAlbedo layer({zone});
    const double radius = std::sqrt(zone.area / terraingen::kPi);
    double sum = 0.0;
    int count = 0;
    for (double x = -radius; x <= radius; x += 1.0) {
        for (double y = -radius; y <= radius; y += 1.0) {
            if (x * x + y * y < radius * radius) {
                sum += layer.Change(zone, x, y);
                ++count;
            }
        }
    }
    EXPECT_NEAR(sum / count, zone.change, 0.003) << "Apollo mean +10.9%";
    EXPECT_GT(layer.Change(zone, 0.0, 0.0), zone.change) << "brightest near the lander";
    EXPECT_NEAR(layer.Change(zone, 1.2 * radius, 0.0), zone.haloChange, 0.01) << "halo";
    EXPECT_EQ(layer.Change(zone, 1.95 * radius, 0.0), 0.0) << "beyond the halo";
    EXPECT_NEAR(BlastZoneAlbedo::Reach(zone), 1.9 * radius, 1e-9);
    EXPECT_GT(terraingen::BlastZoneMeanShare(), 0.2);
    EXPECT_LT(terraingen::BlastZoneMeanShare(), 0.5);
}

TEST(BlastZoneAlbedo, StretchesAlongItsHeadingAndDarkensItsCore) {
    BlastZone zone = Round();
    zone.area = 2530.0;
    zone.elongation = 75.0 / 43.0;
    zone.headingDegrees = 90.0;
    zone.haloReach = 1.0;
    zone.coreRadius = 5.0;
    const BlastZoneAlbedo layer({zone});
    EXPECT_GT(layer.Change(zone, 0.0, 30.0), 0.0) << "inside 37.5 m along the long axis";
    EXPECT_EQ(layer.Change(zone, 30.0, 0.0), 0.0) << "outside 21.5 m across it";
    EXPECT_DOUBLE_EQ(layer.Change(zone, 0.0, 1.0), zone.coreChange);
    EXPECT_GT(layer.Change(zone, 0.0, 6.0), 0.0);

    zone.raggedness = 0.5;
    const BlastZoneAlbedo ragged({zone});
    int inside = 0;
    for (int k = 0; k < 360; ++k) {
        const double a = 2.0 * terraingen::kPi * k / 360.0;
        inside += ragged.Change(zone, 0.75 * 21.5 * std::cos(a), 0.75 * 37.5 * std::sin(a)) > 0.0;
    }
    EXPECT_GT(inside, 30);
    EXPECT_LT(inside, 359) << "lobes pull the edge in";
}

TEST(BlastZoneAlbedo, GridsAndPointsAgreeOnAPlaneAndASphere) {
    for (const terraingen::Surface& surface :
         {terraingen::Surface{}, terraingen::Surface::Sphere(1737400.0)}) {
        BlastZone zone;
        zone.coreRadius = 10.0;
        zone.x = 0.0;
        zone.y = 0.0;
        terraingen::SyntheticAlbedoOptions params;
        params.value = 0.1;
        const Albedo albedo{
            std::make_shared<terraingen::SyntheticAlbedo>(params, terraingen::Vec2d{}, surface),
            {std::make_shared<BlastZoneAlbedo>(std::vector<BlastZone>{zone}, surface)},
            surface};
        const double step = 8.0 / surface.UnitLength();
        const RasterGrid grid{48, 48, GeoTransform::NorthUp(-24.0 * step, 24.0 * step, step)};
        const auto values = terraingen::EvaluateAlbedo(albedo, grid, {8.0});
        EXPECT_LT(values[0](24, 24), 0.1f) << "the dark core";
        EXPECT_GT(values[0](30, 24), 0.1f) << "the bright zone";
        EXPECT_FLOAT_EQ(values[0](0, 0), 0.1f) << "undisturbed";
        for (std::size_t row = 0; row < 48; row += 5) {
            for (std::size_t col = 0; col < 48; col += 3) {
                const auto centre = values[0].CellCenter(col, row);
                ASSERT_EQ(terraingen::EvaluateAlbedoAt(albedo, centre.x, centre.y, {8.0})[0],
                          values[0](col, row));
            }
        }
    }
}

TEST(BlastZone, RejectsInvalidSettings) {
    EXPECT_NO_THROW(terraingen::ValidateBlastZone({}));
    const auto rejects = [](auto change) {
        BlastZone zone;
        change(zone);
        EXPECT_THROW(terraingen::ValidateBlastZone(zone), std::invalid_argument);
        EXPECT_THROW(BlastZoneAlbedo({zone}), std::invalid_argument);
    };
    rejects([](BlastZone& z) { z.x = std::numeric_limits<double>::quiet_NaN(); });
    rejects([](BlastZone& z) { z.area = 0.0; });
    rejects([](BlastZone& z) { z.elongation = 0.9; });
    rejects([](BlastZone& z) { z.haloReach = 0.5; });
    rejects([](BlastZone& z) { z.change = -1.0; });
    rejects([](BlastZone& z) { z.coreChange = std::numeric_limits<double>::infinity(); });
    rejects([](BlastZone& z) { z.coreRadius = -1.0; });
    rejects([](BlastZone& z) { z.raggedness = 1.0; });
}
