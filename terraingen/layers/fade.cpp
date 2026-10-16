#include "terraingen/layers/fade.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace terraingen {

double Smoothstep(double t) {
    t = std::clamp(t, 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

double Falloff(double x, double start, double end, BlendShape shape) {
    if (x <= start) {
        return 1.0;
    }
    if (x >= end) {
        return 0.0;
    }
    const double t = (x - start) / (end - start);
    switch (shape) {
    case BlendShape::Linear:
        return 1.0 - t;
    case BlendShape::Smoothstep:
        return 1.0 - Smoothstep(t);
    case BlendShape::Smootherstep:
        return 1.0 - t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
    case BlendShape::Gaussian: {
        constexpr double kSigmas = 3.0;
        const double tail = std::exp(-0.5 * kSigmas * kSigmas);
        const double z = kSigmas * t;
        return (std::exp(-0.5 * z * z) - tail) / (1.0 - tail);
    }
    }
    return 0.0;
}

void ValidateResolutionFade(const ResolutionFade& fade) {
    if (!(std::isfinite(fade.startSamples) && std::isfinite(fade.endSamples) &&
          fade.startSamples >= 0.0 && fade.endSamples > fade.startSamples)) {
        throw std::invalid_argument("Resolution fade must satisfy 0 <= startSamples < endSamples");
    }
}

double ResolutionWeight(double diameter, double spacing, const ResolutionFade& fade) {
    return 1.0 - Falloff(diameter / spacing, fade.startSamples, fade.endSamples, fade.shape);
}

}  // namespace terraingen
