#include "terraingen/layers/craters/shaping/slope.h"

#include "terraingen/layers/fade.h"
#include "terraingen/random/stream.h"
#include "terraingen/util/checks.h"

#include <cmath>
#include <stdexcept>

namespace terraingen {

namespace {

constexpr std::uint32_t kSlopeStream = 0x534C4F50u;

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
    if (response.criticalGradient) {
        RequirePositive(*response.criticalGradient, "Crater criticalGradient");
    }
    if (!(std::isfinite(response.maxWearRate) && response.maxWearRate >= 1.0)) {
        throw std::invalid_argument("Crater maxWearRate must be finite and at least 1");
    }
}

double SlopeWearRate(const CraterSlopeResponse& response, double east, double north) {
    if (!response.criticalGradient) {
        return 1.0;
    }
    const double share = std::hypot(east, north) / *response.criticalGradient;
    const double room = 1.0 - share * share;
    return room > 1.0 / response.maxWearRate ? 1.0 / room : response.maxWearRate;
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

}  // namespace terraingen
