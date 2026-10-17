#include "terraingen/layers/craters/rays.h"

#include "terraingen/random/stream.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace terraingen {

namespace {

constexpr std::uint32_t kRayStream = 0x52415953u;

}  // namespace

void ValidateRayLayout(const RayLayout& layout) {
    ValidateValueDistribution(layout.count);
    ValidateValueDistribution(layout.widthDegrees);
    if (layout.count.Lowest() < 0.0 || layout.count.Highest() > 64.0) {
        throw std::invalid_argument("Craters must have 0 to 64 rays");
    }
    if (!(layout.widthDegrees.Lowest() > 0.0 && layout.widthDegrees.Highest() < 180.0)) {
        throw std::invalid_argument("Ray widths must be in (0, 180) degrees");
    }
}

std::vector<RayDirection> RayDirections(const Crater& crater, const RayLayout& layout) {
    const IdWords id = SplitId(crater.id);
    RandomStream random(layout.seed, kRayStream, id.low, id.high);
    const double c1 = random.NextDouble();
    const double c2 = random.NextDouble();
    const auto count = static_cast<int>(std::lround(layout.count.Sample(c1, c2)));
    std::vector<RayDirection> rays;
    rays.reserve(static_cast<std::size_t>(std::max(count, 0)));
    for (int i = 0; i < count; ++i) {
        RayDirection ray;
        ray.azimuth = 2.0 * std::numbers::pi * random.NextDouble();
        const double w1 = random.NextDouble();
        const double w2 = random.NextDouble();
        ray.sinHalfWidth =
            std::sin(0.5 * layout.widthDegrees.Sample(w1, w2) * std::numbers::pi / 180.0);
        ray.key = random.NextU32();
        rays.push_back(ray);
    }
    return rays;
}

}  // namespace terraingen
