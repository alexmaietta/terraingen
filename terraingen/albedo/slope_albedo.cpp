#include "terraingen/albedo/slope_albedo.h"

#include "terraingen/util/parallel.h"

#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <utility>

namespace terraingen {

struct SlopeAlbedo::Lattice {
    double i0 = 0.0;  ///< Column of the first node, in steps from the origin.
    double j0 = 0.0;  ///< Row of the first node.
    std::size_t columns = 0;
    std::size_t rows = 0;
    std::vector<double> slopes;  ///< Degrees, row-major; 0 on the outer ring.

    double At(std::size_t i, std::size_t j) const {
        return slopes[j * columns + i];
    }
};

void ValidateSlopeMaturity(const SlopeMaturityOptions& options) {
    const SlopeMaturityOptions& o = options;
    if (!(o.onsetDegrees >= 0.0 && o.onsetDegrees < 90.0)) {
        throw std::invalid_argument("Slope maturity onset must be in [0, 90) degrees");
    }
    if (!(o.maturityPerDegree >= 0.0 && std::isfinite(o.maturityPerDegree))) {
        throw std::invalid_argument("Slope maturity per degree must be finite and not negative");
    }
    if (!(o.backgroundMaturity > 0.0 && std::isfinite(o.maxMaturity) &&
          o.maxMaturity >= o.backgroundMaturity)) {
        throw std::invalid_argument(
            "Slope background maturity must be positive and the cap finite and not below it");
    }
    if (!(o.baseline > 0.0 && std::isfinite(o.baseline))) {
        throw std::invalid_argument("Slope baseline must be positive and finite");
    }
    if (!std::isfinite(o.originReflectance)) {
        throw std::invalid_argument("Slope origin reflectance must be finite");
    }
}

SlopeAlbedo::SlopeAlbedo(SlopeMaturityOptions options, Surface surface)
    : options_(options), surface_(surface) {
    ValidateSlopeMaturity(options_);
    step_ = surface_.IsBody() ? options_.baseline / (surface_.Radius() * std::numbers::pi / 180.0)
                              : options_.baseline;
}

double SlopeAlbedo::MaturityAt(double slopeDegrees) const {
    const double excess =
        options_.maturityPerDegree * std::fmax(0.0, slopeDegrees - options_.onsetDegrees);
    return std::fmin(options_.backgroundMaturity + excess, options_.maxMaturity);
}

SlopeAlbedo::Lattice SlopeAlbedo::ReadLattice(const Bounds2d& region, const Ground& ground) const {
    Lattice lattice;
    lattice.i0 = std::floor(region.minX / step_) - 1.0;
    lattice.j0 = std::floor(region.minY / step_) - 1.0;
    lattice.columns =
        static_cast<std::size_t>(std::floor(region.maxX / step_) + 2.0 - lattice.i0) + 1;
    lattice.rows = static_cast<std::size_t>(std::floor(region.maxY / step_) + 2.0 - lattice.j0) + 1;
    const auto node = [&](std::size_t i, std::size_t j) {
        return Vec2d{(lattice.i0 + static_cast<double>(i)) * step_,
                     (lattice.j0 + static_cast<double>(j)) * step_};
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
                (z(i + 1, j) - z(i - 1, j)) / surface_.Distance(node(i - 1, j), node(i + 1, j));
            const double dy =
                (z(i, j + 1) - z(i, j - 1)) / surface_.Distance(node(i, j - 1), node(i, j + 1));
            const double slope = std::atan(std::hypot(dx, dy)) * 180.0 / std::numbers::pi;
            lattice.slopes[j * lattice.columns + i] = std::isnan(slope) ? 0.0 : slope;
        }
    }
    return lattice;
}

double SlopeAlbedo::Interpolate(const Lattice& lattice, double x, double y) const {
    const double u = x / step_;
    const double v = y / step_;
    const double fu = std::floor(u);
    const double fv = std::floor(v);
    const auto i = static_cast<std::size_t>(fu - lattice.i0);
    const auto j = static_cast<std::size_t>(fv - lattice.j0);
    const double tu = u - fu;
    const double tv = v - fv;
    return (1.0 - tv) * ((1.0 - tu) * lattice.At(i, j) + tu * lattice.At(i + 1, j)) +
           tv * ((1.0 - tu) * lattice.At(i, j + 1) + tu * lattice.At(i + 1, j + 1));
}

double SlopeAlbedo::SlopeAt(double x, double y, const Ground& ground) const {
    return Interpolate(ReadLattice({x, y, x, y}, ground), x, y);
}

void SlopeAlbedo::Mature(AlbedoSample& value, std::size_t bands, double slopeDegrees) const {
    const double maturity = MaturityAt(slopeDegrees);
    if (maturity == options_.backgroundMaturity) {
        return;
    }
    for (std::size_t band = 0; band < bands; ++band) {
        if (!std::isnan(value[band])) {
            value[band] = static_cast<float>(ReflectanceAtMaturity(
                value[band], options_.backgroundMaturity, maturity, options_.originReflectance));
        }
    }
}

void SlopeAlbedo::ApplyAt(AlbedoSample& value, std::size_t bands, double x, double y,
                          const AlbedoContext& context) const {
    ValidateAlbedoContext(context);
    if (context.ground != nullptr) {
        Mature(value, bands, SlopeAt(x, y, *context.ground));
    }
}

void SlopeAlbedo::Apply(std::vector<Raster<float>>& bands, const AlbedoContext& context) const {
    ValidateAlbedoContext(context);
    if (context.ground == nullptr || bands.empty()) {
        return;
    }
    const RasterGrid grid = RasterGrid::FromRaster(bands[0]);
    Bounds2d centres{
        std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity(),
        -std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity()};
    for (const auto& [col, row] : {std::pair<std::size_t, std::size_t>{0, 0},
                                   {grid.width - 1, 0},
                                   {0, grid.height - 1},
                                   {grid.width - 1, grid.height - 1}}) {
        const Vec2d c = bands[0].CellCenter(col, row);
        centres = {std::fmin(centres.minX, c.x), std::fmin(centres.minY, c.y),
                   std::fmax(centres.maxX, c.x), std::fmax(centres.maxY, c.y)};
    }
    const Lattice lattice = ReadLattice(centres, *context.ground);
    ParallelFor(grid.height, context.threads, [&](std::size_t row) {
        for (std::size_t col = 0; col < grid.width; ++col) {
            const Vec2d centre = bands[0].CellCenter(col, row);
            AlbedoSample value{};
            for (std::size_t band = 0; band < bands.size(); ++band) {
                value[band] = bands[band](col, row);
            }
            Mature(value, bands.size(), Interpolate(lattice, centre.x, centre.y));
            for (std::size_t band = 0; band < bands.size(); ++band) {
                bands[band](col, row) = value[band];
            }
        }
    });
}

}  // namespace terraingen
