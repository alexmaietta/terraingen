#include "terraingen/layers/roughness.h"

#include "terraingen/noise/kernels/hash.h"
#include "terraingen/noise/kernels/perlin.h"
#include "terraingen/raster/grid.h"
#include "terraingen/util/log.h"
#include "terraingen/util/parallel.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

namespace terraingen {

namespace {

constexpr int kMaxOctaves = 40;

/// `PerlinStructureFunction(r) / r^2` at r = 2^(k / 4) for k from `kFirstQuarter`, estimated from
/// 2^26 pairs each.
constexpr int kFirstQuarter = -24;
constexpr std::array<double, 33> kStructureOverSquare{
    0.772366, 0.772241, 0.772129, 0.771929, 0.771444, 0.770975, 0.770202, 0.769434, 0.767832,
    0.766070, 0.763032, 0.759311, 0.753675, 0.745950, 0.735267, 0.720564, 0.700391, 0.673043,
    0.636947, 0.589660, 0.530574, 0.459215, 0.377706, 0.291690, 0.209524, 0.141536, 0.094237,
    0.065236, 0.046153, 0.032397, 0.022947, 0.016231, 0.011443};
constexpr double kLargeLag = 0.183120;

}  // namespace

void ValidateRoughnessOptions(const RoughnessOptions& options) {
    if (!(std::isfinite(options.rmsSlope) && options.rmsSlope > 0.0)) {
        throw std::invalid_argument("Roughness rmsSlope must be positive and finite");
    }
    if (!(std::isfinite(options.baseline) && options.baseline > 0.0)) {
        throw std::invalid_argument("Roughness baseline must be positive and finite");
    }
    if (!(options.hurst > 0.0 && options.hurst <= 1.0)) {
        throw std::invalid_argument("Roughness hurst must be in (0, 1]");
    }
    if (!(std::isfinite(options.maxWavelength) && options.minWavelength > 0.0 &&
          options.minWavelength <= options.maxWavelength)) {
        throw std::invalid_argument(
            "Roughness wavelengths must satisfy 0 < minWavelength <= maxWavelength");
    }
    if (std::log2(options.maxWavelength / options.minWavelength) >= kMaxOctaves) {
        throw std::invalid_argument("Roughness wavelengths may span at most " +
                                    std::to_string(kMaxOctaves) + " octaves");
    }
    ValidateResolutionFade(options.fade);
}

double PerlinStructureFunction(double r) {
    if (!(r > 0.0)) {
        return 0.0;
    }
    const double quarters = 4.0 * std::log2(r) - kFirstQuarter;
    if (quarters <= 0.0) {
        return kStructureOverSquare.front() * r * r;
    }
    const auto last = static_cast<double>(kStructureOverSquare.size() - 1);
    if (quarters >= last) {
        return kLargeLag;
    }
    const auto k = static_cast<std::size_t>(quarters);
    const double t = quarters - static_cast<double>(k);
    return ((1.0 - t) * kStructureOverSquare[k] + t * kStructureOverSquare[k + 1]) * r * r;
}

RoughnessLayer::RoughnessLayer(RoughnessOptions options) : options_(options) {
    ValidateRoughnessOptions(options_);
    constexpr double kGoldenAngle = 2.39996322972865332;
    const double shortest = options_.minWavelength * (1.0 - 1e-12);
    double wavelength = options_.maxWavelength;
    for (std::uint32_t i = 0; wavelength >= shortest; ++i, wavelength *= 0.5) {
        const UVec3 hash = Pcg3dHash({options_.seed, i, 0x524F5547u});
        const double angle = kGoldenAngle * static_cast<double>(i);
        octaves_.push_back({wavelength, std::pow(wavelength, options_.hurst), std::cos(angle),
                            std::sin(angle), static_cast<double>(hash.x >> 16) / 65536.0 * 1024.0,
                            static_cast<double>(hash.y >> 16) / 65536.0 * 1024.0, hash.z});
    }
    const double scale = options_.rmsSlope / RmsSlope(options_.baseline);
    for (Octave& octave : octaves_) {
        octave.amplitude *= scale;
    }
}

double RoughnessLayer::RmsSlope(double baseline) const {
    double structure = 0.0;
    for (const Octave& octave : octaves_) {
        structure += octave.amplitude * octave.amplitude *
                     PerlinStructureFunction(baseline / octave.wavelength);
    }
    return std::sqrt(structure) / baseline;
}

std::vector<double> RoughnessLayer::Wavelengths() const {
    std::vector<double> wavelengths;
    for (const Octave& octave : octaves_) {
        wavelengths.push_back(octave.wavelength);
    }
    return wavelengths;
}

float RoughnessLayer::Roughness(double x, double y, double spacing) const {
    double sum = 0.0;
    for (const Octave& octave : octaves_) {
        const double weight = ResolutionWeight(octave.wavelength, spacing, options_.fade);
        if (weight <= 0.0) {
            break;
        }
        const double u = (octave.cos * x + octave.sin * y) / octave.wavelength + octave.offsetX;
        const double v = (octave.cos * y - octave.sin * x) / octave.wavelength + octave.offsetY;
        const double cellU = std::floor(u);
        const double cellV = std::floor(v);
        const float noise = PerlinNoise2(
            static_cast<std::int32_t>(static_cast<std::uint32_t>(static_cast<std::int64_t>(cellU))),
            static_cast<std::int32_t>(static_cast<std::uint32_t>(static_cast<std::int64_t>(cellV))),
            static_cast<float>(u - cellU), static_cast<float>(v - cellV), octave.seed);
        sum += weight * octave.amplitude * static_cast<double>(noise);
    }
    return static_cast<float>(sum);
}

void RoughnessLayer::Apply(Raster<float>& heights, const LayerContext& context,
                           TerrainFeatures& /*features*/) const {
    const Stopwatch stopwatch;
    const double spacing = std::max(context.spacing, RasterGrid::Of(heights).PixelSize());
    ParallelFor(heights.Height(), context.threads, [&](std::size_t row) {
        for (std::size_t col = 0; col < heights.Width(); ++col) {
            float& cell = heights(col, row);
            if (heights.IsNoData(cell)) {
                continue;
            }
            const Vec2d centre = heights.CellCenter(col, row);
            cell += Roughness(centre.x, centre.y, spacing);
        }
    });
    LogInfo("layers", "Added roughness to ", heights.Width(), "x", heights.Height(), " cells in ",
            FormatDuration(stopwatch.Milliseconds()));
}

float RoughnessLayer::ApplyAt(float height, double x, double y, const LayerContext& context) const {
    if (!(std::isfinite(context.spacing) && context.spacing > 0.0)) {
        throw std::invalid_argument("Sample spacing must be positive and finite");
    }
    if (std::isnan(height)) {
        return height;
    }
    return height + Roughness(x, y, context.spacing);
}

}  // namespace terraingen
