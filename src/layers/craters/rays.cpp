// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/layers/craters/rays.h"

#include "terraingen/geometry/geometry.h"
#include "terraingen/layers/craters/crater_fields.h"
#include "terraingen/random/stream.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace terraingen {

void ValidateRayLayout(const RayLayout& layout) {
    CheckRanges(layout);
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
    RandomStream random(layout.seed, kCraterRayStream, id.low, id.high);
    const auto count = static_cast<int>(std::lround(layout.count.Draw(random)));
    std::vector<RayDirection> rays;
    rays.reserve(static_cast<std::size_t>(std::max(count, 0)));
    for (int i = 0; i < count; ++i) {
        RayDirection ray;
        ray.azimuth = 2.0 * kPi * random.NextDouble();
        ray.sinHalfWidth = std::sin(0.5 * layout.widthDegrees.Draw(random) * kDegree);
        ray.key = random.NextU32();
        rays.push_back(ray);
    }
    return rays;
}

}  // namespace terraingen
