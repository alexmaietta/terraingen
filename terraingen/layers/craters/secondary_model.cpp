#include "terraingen/layers/craters/secondary_model.h"

#include "terraingen/layers/fade.h"
#include "terraingen/layers/source_resolved.h"
#include "terraingen/noise/value.h"
#include "terraingen/random/streams.h"
#include "terraingen/util/checks.h"
#include "terraingen/util/geometry.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace terraingen {

namespace {

/// Singer et al. (2020, Table 3): d = a R^-b, a = kSingerScale D^kSingerSizeExponent, b =
/// kSingerRangeExponent ln D, in km.
constexpr double kSingerScale = 0.022;
constexpr double kSingerSizeExponent = 1.96;
constexpr double kSingerRangeExponent = 0.14;

/// The outer range of rays as a multiple of their median length (Elliott et al. 2018).
constexpr double kLongestRayOverMedian = 1.6;

/// How strongly secondaries clump at `u` clump wavelengths along a ray keyed `key`: 0 between
/// clumps, 1 in them.
double ClumpBump(double u, std::uint32_t key) {
    const double n = static_cast<double>(ValueNoise2(static_cast<float>(u), 0.5f, key));
    return Smoothstep(std::clamp(0.5 + 1.5 * n, 0.0, 1.0));
}

/// The mean of `ClumpBump` along a ray, found once.
double MeanClumpBump() {
    static const double mean = [] {
        constexpr int kSamples = 1 << 16;
        double sum = 0.0;
        for (int i = 0; i < kSamples; ++i) {
            sum += ClumpBump(0.0371 * i, kSecondaryClumpStream);
        }
        return sum / kSamples;
    }();
    return mean;
}

}  // namespace

double SingerMaxSecondaryDiameter(double diameter, double range) {
    const double d = diameter / 1000.0;
    const double r = range / 1000.0;
    return 1000.0 * kSingerScale * std::pow(d, kSingerSizeExponent) *
           std::pow(r, -kSingerRangeExponent * std::log(d));
}

double SingerMaxRange(double diameter, double size) {
    const double d = diameter / 1000.0;
    const double logD = std::log(d);
    if (!(logD > 0.0)) {
        return std::numeric_limits<double>::infinity();
    }
    const double a = kSingerScale * std::pow(d, kSingerSizeExponent);
    return 1000.0 * std::pow(a / (size / 1000.0), 1.0 / (kSingerRangeExponent * logD));
}

double SecondaryOnsetRange(double diameter) {
    const double small = 3000.0;
    const double large = 31000.0;
    double rim = 1.8;
    if (diameter >= large) {
        rim = 1.3;
    } else if (diameter > small) {
        const double t = std::log(diameter / small) / std::log(large / small);
        rim = 1.8 + t * (1.3 - 1.8);
    }
    return (0.5 + rim) * diameter;
}

double ElliottMedianRayLength(double diameter, double coefficient, double exponent) {
    return 1000.0 * coefficient * std::pow(0.0005 * diameter, exponent);
}

void ValidateSecondaryCraterOptions(const SecondaryCraterOptions& options) {
    if (!(options.onsetCount && IsPositive(*options.onsetCount))) {
        throw std::invalid_argument("Secondary craters need an onset count: the sources give their "
                                    "sizes, not their number");
    }
    if (!(IsPositive(options.countFraction) && IsPositive(options.sizeSlope))) {
        throw std::invalid_argument("The secondary count fraction and size slope must be positive");
    }
    if (!(IsPositive(options.minDiameter) && options.maxDiameter > options.minDiameter)) {
        throw std::invalid_argument("Secondary diameters must satisfy 0 < min < max");
    }
    if (options.outerRange && !IsPositive(*options.outerRange)) {
        throw std::invalid_argument("The secondary outer range must be positive");
    }
    ValidateCraterSelection(options.selection, "Primary");
    if (!(IsPositive(options.cellSizeFactor) && IsPositive(options.maxExpected))) {
        throw std::invalid_argument("The cell size factor and expected limit must be positive");
    }
    const SecondaryRays& rays = options.rays;
    ValidateRayLayout(rays.layout);
    if (!(IsInUnitInterval(rays.share) && IsInUnitInterval(rays.clustering) &&
          rays.minHalfAngleDegrees > 0.0 && rays.minHalfAngleDegrees < 90.0 &&
          IsPositive(rays.clusterSpacing))) {
        throw std::invalid_argument("Secondary rays need a share and clustering in [0, 1], a floor "
                                    "angle in (0, 90) degrees and a positive cluster spacing");
    }
    const SecondaryShapes& shapes = options.shapes;
    ValidateValueDistribution(shapes.ellipticity);
    if (!(IsInUnitInterval(shapes.elongatedShare) && shapes.ellipticity.Lowest() >= 1.0 &&
          std::isfinite(shapes.ellipticity.Highest()))) {
        throw std::invalid_argument("Secondary shapes need a share in [0, 1] and a bounded "
                                    "ellipticity of at least 1");
    }
    ValidateSourceResolvesPixels(options.sourceResolvesPixels);
}

double SecondaryOuterRange(double diameter, const SecondaryCraterOptions& options) {
    if (options.outerRange) {
        return *options.outerRange * diameter;
    }
    return kLongestRayOverMedian * ElliottMedianRayLength(diameter);
}

std::optional<SecondaryRayField> SecondaryRayField::Make(const Crater& primary,
                                                         const SecondaryRays& rays) {
    if (!rays.enabled) {
        return std::nullopt;
    }
    SecondaryRayField field;
    field.rays = RayDirections(primary, rays.layout);
    if (field.rays.empty()) {
        return std::nullopt;
    }
    field.diameter = primary.diameter;
    field.onset = SecondaryOnsetRange(primary.diameter);
    field.share = rays.share;
    field.sinFloor = std::sin(rays.minHalfAngleDegrees * kDegree);
    field.clustering = rays.clustering;
    field.spacing = rays.clusterSpacing;
    field.meanClump = MeanClumpBump();
    return field;
}

double SecondaryRayField::Width(const RayDirection& ray, double range) const {
    return std::fmax(diameter * ray.sinHalfWidth, range * sinFloor);
}

double SecondaryRayField::Profile(const RayDirection& ray, double range, double azimuth) const {
    const double delta = std::remainder(azimuth - ray.azimuth, 2.0 * kPi);
    if (std::cos(delta) <= 0.0) {
        return 0.0;
    }
    const double across = range * std::sin(std::fabs(delta)) / Width(ray, range);
    return std::exp(-across * across);
}

double SecondaryRayField::Clump(const RayDirection& ray, double range) const {
    const double u = std::log(range / onset) / spacing;
    return (1.0 - clustering) + clustering * ClumpBump(u, ray.key) / meanClump;
}

double SecondaryRayField::MeanProfile(double range) const {
    double sum = 0.0;
    for (const RayDirection& ray : rays) {
        sum += std::sqrt(kPi) * Width(ray, range);
    }
    return sum / (2.0 * kPi * range);
}

double SecondaryRayField::Density(double range, double azimuth) const {
    double sum = 0.0;
    for (const RayDirection& ray : rays) {
        sum += Profile(ray, range, azimuth) * Clump(ray, range);
    }
    return (1.0 - share) + share * sum / MeanProfile(range);
}

double SecondaryRayField::Peak(double r0, double r1) const {
    double peak = 0.0;
    for (const double range : {r0, 0.5 * (r0 + r1), r1}) {
        for (const RayDirection& at : rays) {
            double sum = 0.0;
            for (const RayDirection& ray : rays) {
                sum += Profile(ray, range, at.azimuth);
            }
            peak = std::fmax(peak, sum / MeanProfile(range));
        }
    }
    const double clump = (1.0 - clustering) + clustering / meanClump;
    return (1.0 - share) + share * 1.1 * peak * clump;
}

SecondaryDensity::SecondaryDensity(const Crater& primary, const SecondaryCraterOptions& options)
    : sizeSlope_(options.sizeSlope), minSize_(options.minDiameter), maxSize_(options.maxDiameter),
      diameter_(primary.diameter) {
    if (!options.selection.Includes(primary) || !options.onsetCount) {
        return;
    }
    onset_ = SecondaryOnsetRange(diameter_);
    const double outer = SecondaryOuterRange(diameter_, options);
    if (!(outer > onset_)) {
        return;
    }
    outer_ = outer;
    density0_ = *options.onsetCount / (0.25 * kPi * diameter_ * diameter_);
    reference_ = options.countFraction * diameter_;
    rays_ = SecondaryRayField::Make(primary, options.rays);
}

double SecondaryDensity::Measure(const Vec2d& offset, double dMin, double dMax, bool cover) const {
    const double range = std::hypot(offset.x, offset.y);
    if (!(range >= onset_ && range < outer_)) {
        return 0.0;
    }
    const double low = std::fmax(dMin, minSize_);
    const double high =
        std::fmin(std::fmin(dMax, maxSize_), SingerMaxSecondaryDiameter(diameter_, range));
    if (!(high > low)) {
        return 0.0;
    }
    const double r = ScaledRange(range);
    const double s = sizeSlope_;
    double sizes = SizeShare(low, high);
    if (cover) {
        const double scale = 0.25 * kPi * s * std::pow(reference_, s);
        sizes = s == 2.0 ? scale * std::log(high / low)
                         : scale * (std::pow(high, 2.0 - s) - std::pow(low, 2.0 - s)) / (2.0 - s);
    }
    const double density = density0_ / (r * r) * sizes;
    return rays_ ? density * rays_->Density(range, std::atan2(offset.y, offset.x)) : density;
}

double SecondaryDensity::At(const Vec2d& offset, double dMin, double dMax) const {
    return Measure(offset, dMin, dMax, false);
}

double SecondaryDensity::CoverAt(const Vec2d& offset, double dMin, double dMax) const {
    return Measure(offset, dMin, dMax, true);
}

double SecondaryDensity::SizeShare(double low, double high) const {
    return std::pow(low / reference_, -sizeSlope_) - std::pow(high / reference_, -sizeSlope_);
}

double LargestSecondary(double diameter, const SecondaryCraterOptions& options) {
    const double onset = SecondaryOnsetRange(diameter);
    const double outer = SecondaryOuterRange(diameter, options);
    if (!(outer > onset)) {
        return 0.0;
    }
    return std::fmin(options.maxDiameter, std::fmax(SingerMaxSecondaryDiameter(diameter, onset),
                                                    SingerMaxSecondaryDiameter(diameter, outer)));
}

}  // namespace terraingen
