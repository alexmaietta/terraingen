// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/layers/strata.h"

#include "terraingen/geometry/surface.h"
#include "terraingen/layers/anchored_plane.h"
#include "terraingen/layers/fade.h"
#include "terraingen/layers/pavement_fields.h"
#include "terraingen/random/stream.h"
#include "terraingen/util/bits.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <optional>
#include <stdexcept>

namespace terraingen {

namespace {

constexpr const char* kGroundReader = "A strata material cover";
/// Width of the fade from bench to cliff in the material map, in bed heights.
constexpr double kCliffFade = 0.05;

}  // namespace

void ValidateStrataOptions(const StrataOptions& options) {
    CheckRanges(options);
}

StrataLayer::StrataLayer(StrataOptions options) : m_options(options) {
    ValidateStrataOptions(m_options);
    m_dipDirection = {std::cos(m_options.dipAzimuthDegrees * kDegree),
                      std::sin(m_options.dipAzimuthDegrees * kDegree)};
    m_dipSlope = std::tan(m_options.dipDegrees * kDegree);
}

LayerPtr StrataLayer::Reseeded(std::uint64_t seed) const {
    StrataOptions options = m_options;
    options.seed = ReseedWord(seed, 0);
    return std::make_shared<StrataLayer>(options);
}

RandomStream StrataLayer::BedStream(std::uint32_t stream, std::int64_t k) const {
    const IdWords bed = SplitId(static_cast<std::uint64_t>(k));
    return RandomStream(m_options.seed, stream, bed.low, bed.high);
}

double StrataLayer::BedBoundary(std::int64_t k) const {
    RandomStream random = BedStream(kStrataStream, k);
    const double offset = m_options.thicknessVariation * (random.NextDouble() - 0.5);
    return m_options.bedThickness * (static_cast<double>(k) + offset);
}

StrataLayer::Bed StrataLayer::BedAt(double level) const {
    auto k = static_cast<std::int64_t>(std::floor(level / m_options.bedThickness));
    double bottom = BedBoundary(k);
    while (level < bottom) {
        --k;
        bottom = BedBoundary(k);
    }
    double top = BedBoundary(k + 1);
    while (level >= top) {
        ++k;
        bottom = top;
        top = BedBoundary(k + 1);
    }
    const bool hard = m_options.hardShare >= 1.0 ||
                      BedStream(kStrataHardnessStream, k).NextDouble() < m_options.hardShare;
    return {bottom, top, hard};
}

bool StrataLayer::StratigraphicLevel(double height, double x, double y, const Surface& surface,
                                     double& level) const {
    const std::optional<Vec2d> q = AnchoredPlane(surface, m_options.anchor).Project(x, y);
    if (!q) {
        return false;
    }
    level = height + m_dipSlope * (q->x * m_dipDirection.x + q->y * m_dipDirection.y);
    return std::isfinite(level);
}

double StrataLayer::TerracedAt(double height, double x, double y, const Surface& surface) const {
    double level = 0.0;
    if (!StratigraphicLevel(height, x, y, surface, level) || m_options.strength <= 0.0) {
        return height;
    }
    const Bed bed = BedAt(level);
    const double rise = bed.top - bed.bottom;
    const double u = std::clamp((level - bed.bottom) / rise, 0.0, 1.0);
    const double sharpness = bed.hard ? m_options.sharpness : m_options.softSharpness;
    const double terraced = height + rise * (std::pow(u, sharpness) - u);
    return height + m_options.strength * (terraced - height);
}

double StrataLayer::MaterialCoverAt(double x, double y, const LayerContext& context) const {
    RequireGround(context.ground, kGroundReader);
    const double height = static_cast<double>(context.ground->HeightAt(x, y, context.spacing));
    if (!std::isfinite(height) || m_options.strength <= 0.0) {
        return 0.0;
    }
    double level = 0.0;
    if (!StratigraphicLevel(height, x, y, context.SurfaceOrPlane(), level)) {
        return 0.0;
    }
    const Bed bed = BedAt(level);
    if (!bed.hard) {
        return 0.0;
    }
    const double u = std::clamp((level - bed.bottom) / (bed.top - bed.bottom), 0.0, 1.0);
    return Smoothstep((u - (1.0 - m_options.cliffShare)) / kCliffFade + 0.5);
}

float StrataLayer::HeightAt(float height, double x, double y, const LayerContext& context) const {
    return static_cast<float>(
        TerracedAt(static_cast<double>(height), x, y, context.SurfaceOrPlane()));
}

}  // namespace terraingen
