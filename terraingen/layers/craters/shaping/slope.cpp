#include "terraingen/layers/craters/shaping/slope.h"

#include "terraingen/layers/fade.h"
#include "terraingen/random/stream.h"
#include "terraingen/util/checks.h"

#include <cmath>
#include <stdexcept>

namespace terraingen {

namespace {

constexpr std::uint32_t kSlopeStream = 0x534C4F50u;

/// Where the floor shift starts to fade, in radii.
constexpr double kShiftPlateauRadii = 0.3;

/// The largest floor offset in diameters that keeps the shifted interior from folding.
constexpr double kMaxFloorOffset = 0.23;

}  // namespace

void ValidateCraterSlopeResponse(const CraterSlopeResponse& response) {
    ValidateValueDistribution(response.floorOffset);
    if (!(response.floorOffset.Lowest() >= 0.0 &&
          response.floorOffset.Highest() < kMaxFloorOffset)) {
        throw std::invalid_argument("Crater floor offset must be in [0, 0.23) diameters");
    }
    RequireUnitInterval(response.upslopeRim, "Crater upslopeRim");
    RequirePositive(response.fullSlope, "Crater fullSlope");
}

CraterSlopeShape SlopeShape(const Crater& crater, double east, double north,
                            const CraterSlopeResponse& response) {
    const double gradient = std::hypot(east, north);
    if (!response.enabled || !(gradient > 0.0)) {
        return {};
    }
    const double strength = Smoothstep(std::atan(gradient) / response.fullSlope);
    const IdWords id = SplitId(crater.id);
    RandomStream random(response.seed, kSlopeStream, id.low, id.high);
    const double u1 = random.NextDouble();
    const double u2 = random.NextDouble();
    const double offset = strength * response.floorOffset.Sample(u1, u2) * crater.diameter;
    const Vec2d up{east / gradient, north / gradient};
    const double removed = strength * (1.0 - response.upslopeRim);
    return {{-offset * up.x, -offset * up.y}, {removed * up.x, removed * up.y}};
}

Vec2d SlopeShiftedOffset(const CraterSlopeShape& shape, const Vec2d& offset, double radius) {
    const double radii = std::hypot(offset.x, offset.y) / radius;
    const double weight =
        1.0 - Smoothstep((radii - kShiftPlateauRadii) / (1.0 - kShiftPlateauRadii));
    return {offset.x - weight * shape.shift.x, offset.y - weight * shape.shift.y};
}

double SlopeRaisedShare(const CraterSlopeShape& shape, const Vec2d& offset) {
    const double distance = std::hypot(offset.x, offset.y);
    const double removed = std::hypot(shape.upslope.x, shape.upslope.y);
    if (!(distance > 0.0) || !(removed > 0.0)) {
        return 1.0;
    }
    const double cosine = std::fmax(0.0, (shape.upslope.x * offset.x + shape.upslope.y * offset.y) /
                                             (removed * distance));
    return 1.0 - removed * cosine * cosine;
}

}  // namespace terraingen
