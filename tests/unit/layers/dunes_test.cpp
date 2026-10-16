#include "terraingen/layers/dunes.h"

#include "terraingen/terraingen.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <numbers>
#include <stdexcept>
#include <vector>

using terraingen::DuneLayer;
using terraingen::DuneOptions;
using terraingen::DuneShape;
using terraingen::Vec2d;

namespace {

constexpr double kDegree = std::numbers::pi / 180.0;

/// One barchan of `width` in a cell far bigger than it.
DuneOptions OneBarchan(double width = 400.0) {
    DuneOptions options;
    options.width = width;
    options.widthSpread = 0.0;
    options.spacing = 6000.0;
    options.density = 1.0;
    options.seed = 7;
    return options;
}

/// The highest point of `layer` over [0, extent)^2, refined from a coarse search.
Vec2d Peak(const DuneLayer& layer, double extent, double step) {
    Vec2d best{};
    double top = -1.0;
    for (double y = 0.5 * step; y < extent; y += step) {
        for (double x = 0.5 * step; x < extent; x += step) {
            const double h = layer.DuneHeightAt(x, y);
            if (h > top) {
                top = h;
                best = {x, y};
            }
        }
    }
    for (double fine : {step / 10.0, step / 100.0}) {
        const Vec2d around = best;
        for (double y = around.y - 10.0 * fine; y <= around.y + 10.0 * fine; y += fine) {
            for (double x = around.x - 10.0 * fine; x <= around.x + 10.0 * fine; x += fine) {
                const double h = layer.DuneHeightAt(x, y);
                if (h > top) {
                    top = h;
                    best = {x, y};
                }
            }
        }
    }
    return best;
}

/// The relief `layer` adds at (x, y), ripples included, at a 1 cm spacing.
double At(const DuneLayer& layer, double x, double y, double spacing = 0.01) {
    return static_cast<double>(layer.ApplyAt(0.0f, x, y, {spacing}));
}

/// The steepest slope of `layer`'s dunes over a square of `half` metres about `centre`.
double SteepestSlope(const DuneLayer& layer, const Vec2d& centre, double half, double step) {
    double steepest = 0.0;
    for (double y = centre.y - half; y <= centre.y + half; y += step) {
        for (double x = centre.x - half; x <= centre.x + half; x += step) {
            const double gx =
                (layer.DuneHeightAt(x + 0.5 * step, y) - layer.DuneHeightAt(x - 0.5 * step, y)) /
                step;
            const double gy =
                (layer.DuneHeightAt(x, y + 0.5 * step) - layer.DuneHeightAt(x, y - 0.5 * step)) /
                step;
            steepest = std::max(steepest, std::atan(std::hypot(gx, gy)));
        }
    }
    return steepest;
}

}  // namespace

TEST(DuneLayer, BarchansHaveSlipFacesAtTheirAngleAndHornsDownwind) {
    const DuneOptions options = OneBarchan();
    const DuneLayer layer(options);
    const Vec2d peak = Peak(layer, options.spacing, 50.0);
    const double height = options.heightRatio * options.width;
    EXPECT_NEAR(layer.DuneHeightAt(peak.x, peak.y), height, 0.01 * height);

    const double lee =
        (layer.DuneHeightAt(peak.x + 4.0, peak.y) - layer.DuneHeightAt(peak.x + 6.0, peak.y)) / 2.0;
    EXPECT_NEAR(std::atan(lee), options.slipFaceAngle, 0.2 * kDegree) << "the slip face";
    const double foot = peak.x + height / std::tan(options.slipFaceAngle);
    EXPECT_EQ(layer.DuneHeightAt(foot + 1.0, peak.y), 0.0);

    const double toe = peak.x - height / std::tan(options.stossSlope);
    EXPECT_GT(layer.DuneHeightAt(toe + 2.0, peak.y), 0.0);
    EXPECT_EQ(layer.DuneHeightAt(toe - 2.0, peak.y), 0.0) << "the stoss is H / tan(stoss) long";

    const double horns = options.hornLength * height;
    const double past = peak.x + 0.6 * horns;
    for (const double side : {-1.0, 1.0}) {
        EXPECT_GT(layer.DuneHeightAt(past, peak.y + side * 0.45 * options.width), 0.0)
            << "a horn reaches downwind";
    }
    EXPECT_EQ(layer.DuneHeightAt(past, peak.y), 0.0) << "between the horns";
    EXPECT_EQ(layer.DuneHeightAt(peak.x, peak.y + 0.51 * options.width), 0.0);
}

TEST(DuneLayer, SmallDunesAndDomesHaveNoSlipFace) {
    for (const DuneShape shape : {DuneShape::Barchan, DuneShape::Dome}) {
        DuneOptions options = OneBarchan(150.0);
        options.shape = shape;
        const DuneLayer layer(options);
        const Vec2d peak = Peak(layer, options.spacing, 50.0);
        EXPECT_GT(layer.DuneHeightAt(peak.x, peak.y), 0.0);
        EXPECT_LT(SteepestSlope(layer, peak, 160.0, 2.0), 15.0 * kDegree);
    }
    DuneOptions dome = OneBarchan(500.0);
    dome.shape = DuneShape::Dome;
    const DuneLayer layer(dome);
    EXPECT_LT(SteepestSlope(layer, Peak(layer, dome.spacing, 50.0), 400.0, 4.0), 15.0 * kDegree)
        << "domes of any size";
}

TEST(DuneLayer, TransverseCrestsKeepTheirSpacingAndTheSlipFaceAngle) {
    DuneOptions options;
    options.shape = DuneShape::Transverse;
    options.sinuosity = 0.0;
    options.defects = 0.0;
    options.wavelength = 250.0;
    options.height = 20.0;
    const DuneLayer layer(options);
    int crests = 0;
    double previous = layer.DuneHeightAt(-0.5, 37.0);
    double current = layer.DuneHeightAt(0.0, 37.0);
    for (double x = 0.5; x < 10.0 * options.wavelength; x += 0.5) {
        const double next = layer.DuneHeightAt(x, 37.0);
        if (current > previous && current >= next && current > 0.9 * options.height) {
            ++crests;
        }
        previous = current;
        current = next;
    }
    EXPECT_EQ(crests, 10);
    EXPECT_NEAR(layer.DuneHeightAt(123.0, -400.0), layer.DuneHeightAt(123.0, 900.0), 1e-9)
        << "straight crests across the wind";

    options.sinuosity = 0.4;
    options.defects = 0.3;
    const DuneLayer warped(options);
    std::vector<double> slopes;
    for (double y = -200.0; y < 600.0; y += 1.0) {
        for (double x = -100.0; x < 700.0; x += 1.0) {
            const double gx = warped.DuneHeightAt(x + 0.05, y) - warped.DuneHeightAt(x - 0.05, y);
            const double gy = warped.DuneHeightAt(x, y + 0.05) - warped.DuneHeightAt(x, y - 0.05);
            slopes.push_back(std::atan(std::hypot(gx, gy) / 0.1));
        }
    }
    std::sort(slopes.begin(), slopes.end());
    EXPECT_LT(slopes[slopes.size() * 995 / 1000], options.slipFaceAngle + 3.0 * kDegree)
        << "warped lee faces stay within a few degrees of the slip face angle";
    EXPECT_GT(slopes[slopes.size() * 995 / 1000], options.slipFaceAngle - 2.0 * kDegree);
    EXPECT_LT(slopes.back(), options.slipFaceAngle + 6.0 * kDegree);
}

TEST(DuneLayer, LinearCrestsRunAlongTheWind) {
    DuneOptions options;
    options.shape = DuneShape::Linear;
    options.sinuosity = 0.0;
    options.defects = 0.0;
    options.windAzimuth = 0.0;
    const DuneLayer layer(options);
    EXPECT_NEAR(layer.DuneHeightAt(-800.0, 75.0), layer.DuneHeightAt(1300.0, 75.0), 1e-9);
    double top = 0.0;
    for (double y = 0.0; y < options.wavelength; y += 0.5) {
        top = std::max(top, layer.DuneHeightAt(0.0, y));
    }
    EXPECT_NEAR(top, options.height, 0.01 * options.height);
    EXPECT_LT(SteepestSlope(layer, {0.0, 0.0}, 300.0, 1.0), 31.0 * kDegree);

    options.shape = DuneShape::Barchanoid;
    const DuneLayer barchanoid(options);
    EXPECT_NE(barchanoid.DuneHeightAt(0.0, 0.0), barchanoid.DuneHeightAt(0.0, 200.0))
        << "crests vary along their length";
}

TEST(DuneLayer, RipplesLieOnDuneSandAndForkWithoutSeams) {
    DuneOptions options = OneBarchan();
    options.ripples = terraingen::MarsDuneRipples();
    options.ripples.forks = 1.0;
    const auto layer = std::make_shared<DuneLayer>(options);
    const Vec2d peak = Peak(*layer, options.spacing, 50.0);
    const Vec2d stoss{peak.x - 60.0, peak.y};
    double low = 1e9;
    double high = -1e9;
    double jump = 0.0;
    double before = At(*layer, stoss.x, stoss.y) - layer->DuneHeightAt(stoss.x, stoss.y);
    for (double d = 0.01; d < 60.0; d += 0.01) {
        const double x = stoss.x + d * 0.6;
        const double y = stoss.y + d * 0.8;
        const double ripple = At(*layer, x, y) - layer->DuneHeightAt(x, y);
        low = std::min(low, ripple);
        high = std::max(high, ripple);
        jump = std::max(jump, std::fabs(ripple - before));
        before = ripple;
    }
    EXPECT_GT(high - low, 0.5 * options.ripples.height);
    EXPECT_LE(high, options.ripples.height * 1.0001);
    EXPECT_GE(low, -1e-5);
    EXPECT_LT(jump, 0.02) << "fork cuts jump a whole period, which the profile hides";

    const Vec2d bare{peak.x, peak.y + 0.6 * options.width};
    EXPECT_EQ(layer->DuneHeightAt(bare.x, bare.y), 0.0);
    EXPECT_EQ(At(*layer, bare.x, bare.y), 0.0) << "no interdune sand";
    options.ripples.interduneCover = 1.0;
    const DuneLayer covered(options);
    double ripples = 0.0;
    for (double d = 0.0; d < 5.0; d += 0.1) {
        ripples = std::max(ripples, At(covered, bare.x + d, bare.y));
    }
    EXPECT_GT(ripples, 0.5 * options.ripples.height);
}

TEST(DuneLayer, GridsAndPointsAgreeAndCoarseSpacingsFadeTheDunesOut) {
    DuneOptions options = OneBarchan();
    options.spacing = 500.0;
    options.ripples = terraingen::MarsDuneRipples();
    const DuneLayer layer(options);
    const terraingen::RasterGrid grid{64, 48, terraingen::GeoTransform::NorthUp(-40.0, 300.0, 0.5)};
    terraingen::Raster<float> heights = grid.MakeRaster();
    layer.Apply(heights, {0.5, 2});
    double relief = 0.0;
    for (std::size_t row = 0; row < grid.height; ++row) {
        for (std::size_t col = 0; col < grid.width; ++col) {
            const Vec2d centre = heights.CellCenter(col, row);
            ASSERT_EQ(layer.ApplyAt(0.0f, centre.x, centre.y, {0.5}), heights(col, row));
            relief = std::max(relief, static_cast<double>(heights(col, row)));
        }
    }
    EXPECT_GT(relief, 0.0);
    EXPECT_EQ(At(layer, 10.0, 20.0, 1e5), 0.0) << "faded out";
}

TEST(DuneLayer, LaysDunesOutInTheAnchorsTangentFrameOnABody) {
    const terraingen::Surface mars = terraingen::Surface::Sphere(3389500.0);
    DuneOptions options;
    options.shape = DuneShape::Transverse;
    options.anchor = {137.4, -4.6};
    const DuneLayer body(options, mars);
    const DuneLayer plane(options);
    for (const Vec2d point : {Vec2d{137.41, -4.59}, Vec2d{137.5, -4.7}}) {
        const Vec2d offset = mars.Offset(options.anchor, point);
        EXPECT_NEAR(body.DuneHeightAt(point.x, point.y), plane.DuneHeightAt(offset.x, offset.y),
                    1e-9);
    }
    EXPECT_EQ(body.DuneHeightAt(-42.6, 4.6), 0.0) << "nothing on the far hemisphere";
}

TEST(DuneLayer, RejectsBadOptions) {
    const auto rejects = [](auto change) {
        DuneOptions options;
        change(options);
        EXPECT_THROW(DuneLayer{options}, std::invalid_argument);
    };
    rejects([](DuneOptions& o) { o.slipFaceAngle = 0.0; });
    rejects([](DuneOptions& o) { o.slipFaceAngle = 2.0; });
    rejects([](DuneOptions& o) { o.stossSlope = o.slipFaceAngle; });
    rejects([](DuneOptions& o) { o.width = 0.0; });
    rejects([](DuneOptions& o) { o.spacing = -1.0; });
    rejects([](DuneOptions& o) { o.density = 1.5; });
    rejects([](DuneOptions& o) { o.widthSpread = 1.0; });
    rejects([](DuneOptions& o) { o.defects = -0.1; });
    rejects([](DuneOptions& o) { o.sinuosity = -1.0; });
    rejects([](DuneOptions& o) { o.anchor.x = NAN; });
    rejects([](DuneOptions& o) { o.ripples.leeFraction = 1.0; });
    rejects([](DuneOptions& o) { o.ripples.forks = 2.0; });
    rejects([](DuneOptions& o) { o.ripples.interduneCover = -0.5; });
    rejects([](DuneOptions& o) { o.fade.endSamples = 0.0; });
    EXPECT_NO_THROW(DuneLayer{DuneOptions{}});
}

TEST(DuneLayer, MarsPresetsCarryTheirPublishedValues) {
    const DuneOptions barchans = terraingen::MarsBarchans();
    EXPECT_EQ(barchans.shape, DuneShape::Barchan);
    EXPECT_NEAR(barchans.slipFaceAngle, 29.0 * kDegree, 1e-12);
    EXPECT_NEAR(barchans.stossSlope, 8.0 * kDegree, 1e-12);
    EXPECT_EQ(barchans.minWidth, 200.0);
    EXPECT_EQ(barchans.domeLength, 2.0);
    const DuneOptions transverse = terraingen::MarsTransverseDunes();
    EXPECT_EQ(transverse.shape, DuneShape::Transverse);
    EXPECT_EQ(transverse.wavelength, 248.0);
    EXPECT_EQ(transverse.height, 28.0);
    const terraingen::DuneRipples ripples = terraingen::MarsDuneRipples();
    EXPECT_EQ(ripples.wavelength, 2.1);
    EXPECT_EQ(ripples.height, 0.2);
    EXPECT_NEAR(std::atan(ripples.height / (ripples.leeFraction * ripples.wavelength)),
                30.0 * kDegree, 1e-12)
        << "a 30 deg lee";
    EXPECT_NO_THROW(DuneLayer{barchans});
    EXPECT_NO_THROW(DuneLayer{transverse});
}
