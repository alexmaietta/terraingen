// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/albedo/slope_albedo.h"

#include "albedo/bands.h"
#include "terraingen/albedo/albedo_fields.h"
#include "terraingen/geometry/geometry.h"

#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace terraingen {

struct SlopeAlbedo::Lattice {
    double step = 1.0;  ///< Step between nodes in surface coordinates.
    double i0 = 0.0;    ///< Column of the first node, in steps from the origin.
    double j0 = 0.0;    ///< Row of the first node.
    std::size_t columns = 0;
    std::size_t rows = 0;
    std::vector<double> slopes;  ///< Degrees, row-major; 0 on the outer ring.

    double At(std::size_t i, std::size_t j) const {
        return slopes[j * columns + i];
    }
};

void ValidateSlopeAlbedoOptions(const SlopeAlbedoOptions& options) {
    CheckRanges(options);
    if (!(options.maxMaturity >= options.backgroundMaturity)) {
        throw std::invalid_argument("Slope maturity cap must not be below the background");
    }
}

SlopeAlbedo::SlopeAlbedo(SlopeAlbedoOptions options) : options_(options) {
    ValidateSlopeAlbedoOptions(options_);
}

double SlopeAlbedo::MaturityAt(double slopeDegrees) const {
    const double excess =
        options_.maturityPerDegree * std::fmax(0.0, slopeDegrees - options_.onsetDegrees);
    return std::fmin(options_.backgroundMaturity + excess, options_.maxMaturity);
}

SlopeAlbedo::Lattice SlopeAlbedo::ReadLattice(const Bounds2d& region, const Ground& ground,
                                              const Surface& surface) const {
    Lattice lattice;
    const double step = options_.baseline / surface.UnitLength();
    lattice.step = step;
    lattice.i0 = std::floor(region.minX / step) - 1.0;
    lattice.j0 = std::floor(region.minY / step) - 1.0;
    lattice.columns =
        static_cast<std::size_t>(std::floor(region.maxX / step) + 2.0 - lattice.i0) + 1;
    lattice.rows = static_cast<std::size_t>(std::floor(region.maxY / step) + 2.0 - lattice.j0) + 1;
    const auto node = [&](std::size_t i, std::size_t j) {
        return Vec2d{(lattice.i0 + static_cast<double>(i)) * step,
                     (lattice.j0 + static_cast<double>(j)) * step};
    };
    std::vector<Vec2d> points;
    points.reserve(lattice.columns * lattice.rows);
    for (std::size_t j = 0; j < lattice.rows; ++j) {
        for (std::size_t i = 0; i < lattice.columns; ++i) {
            points.push_back(node(i, j));
        }
    }
    std::vector<float> heights(points.size());
    ground.HeightsAt(points, heights, options_.baseline);
    lattice.slopes.assign(points.size(), 0.0);
    for (std::size_t j = 1; j + 1 < lattice.rows; ++j) {
        for (std::size_t i = 1; i + 1 < lattice.columns; ++i) {
            const auto z = [&](std::size_t a, std::size_t b) {
                return static_cast<double>(heights[b * lattice.columns + a]);
            };
            const double dx =
                (z(i + 1, j) - z(i - 1, j)) / surface.Distance(node(i - 1, j), node(i + 1, j));
            const double dy =
                (z(i, j + 1) - z(i, j - 1)) / surface.Distance(node(i, j - 1), node(i, j + 1));
            const double slope = GradientSlopeDegrees(dx, dy);
            lattice.slopes[j * lattice.columns + i] = std::isnan(slope) ? 0.0 : slope;
        }
    }
    return lattice;
}

double SlopeAlbedo::Interpolate(const Lattice& lattice, double x, double y) {
    const double u = x / lattice.step;
    const double v = y / lattice.step;
    const double fu = std::floor(u);
    const double fv = std::floor(v);
    const auto i = static_cast<std::size_t>(fu - lattice.i0);
    const auto j = static_cast<std::size_t>(fv - lattice.j0);
    const double tu = u - fu;
    const double tv = v - fv;
    return (1.0 - tv) * ((1.0 - tu) * lattice.At(i, j) + tu * lattice.At(i + 1, j)) +
           tv * ((1.0 - tu) * lattice.At(i, j + 1) + tu * lattice.At(i + 1, j + 1));
}

double SlopeAlbedo::SlopeAt(double x, double y, const Ground& ground,
                            const Surface& surface) const {
    return Interpolate(ReadLattice({x, y, x, y}, ground, surface), x, y);
}

void SlopeAlbedo::Mature(AlbedoSample& value, std::size_t bands, double slopeDegrees) const {
    const double maturity = MaturityAt(slopeDegrees);
    if (maturity != options_.backgroundMaturity) {
        MatureBands(value, bands, options_.backgroundMaturity, maturity,
                    options_.originReflectance);
    }
}

void SlopeAlbedo::ApplyAt(AlbedoSample& value, std::size_t bands, double x, double y,
                          const LayerContext& context) const {
    if (context.ground != nullptr) {
        Mature(value, bands, SlopeAt(x, y, *context.ground, context.SurfaceOrPlane()));
    }
}

namespace {

/// The box of the cell centers of `raster`.
Bounds2d CellCenterBounds(const Raster<float>& raster) {
    const std::size_t lastCol = raster.Width() - 1;
    const std::size_t lastRow = raster.Height() - 1;
    Bounds2d centers{
        std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity(),
        -std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity()};
    for (const auto& [col, row] : {std::pair<std::size_t, std::size_t>{0, 0},
                                   {lastCol, 0},
                                   {0, lastRow},
                                   {lastCol, lastRow}}) {
        const Vec2d c = raster.CellCenter(col, row);
        centers = {std::fmin(centers.minX, c.x), std::fmin(centers.minY, c.y),
                   std::fmax(centers.maxX, c.x), std::fmax(centers.maxY, c.y)};
    }
    return centers;
}

}  // namespace

void SlopeAlbedo::Apply(std::vector<Raster<float>>& bands, const LayerContext& context) const {
    if (context.ground == nullptr || bands.empty()) {
        return;
    }
    const Lattice lattice =
        ReadLattice(CellCenterBounds(bands[0]), *context.ground, context.SurfaceOrPlane());
    const std::size_t count = bands.size();
    ForEachAlbedoCell(bands, context.threads, [&](AlbedoSample& value, double x, double y) {
        Mature(value, count, Interpolate(lattice, x, y));
    });
}

}  // namespace terraingen
