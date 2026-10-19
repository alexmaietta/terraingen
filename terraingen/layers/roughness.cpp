#include "terraingen/layers/roughness.h"

#include "terraingen/noise/hash.h"
#include "terraingen/noise/perlin.h"
#include "terraingen/random/streams.h"
#include "terraingen/raster/cell_loops.h"
#include "terraingen/raster/grid.h"
#include "terraingen/util/bits.h"
#include "terraingen/util/checks.h"
#include "terraingen/util/log.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

namespace terraingen {

namespace {

constexpr int kMaxOctaves = 40;

constexpr std::uint32_t kOffsetZSalt = 7u;

/// The golden angle, in radians, by which each octave is turned from the last.
constexpr double kGoldenAngle = 2.39996322972865332;

/// A uniform in [0, 1024) from the top 16 bits of a hash, for lattice offsets.
double LatticeOffset(std::uint32_t hash) {
    return static_cast<double>(hash >> 16) / 65536.0 * 1024.0;
}

/// The rotation matrix, row-major, of a uniform random rotation from three uniform hashes, by
/// Shoemake's (1992) unit quaternion.
std::array<double, 9> UniformRotation(const UVec3& turn) {
    const double u1 = HashToUnitFloat(turn.x);
    const double u2 = 2.0 * kPi * static_cast<double>(HashToUnitFloat(turn.y));
    const double u3 = 2.0 * kPi * static_cast<double>(HashToUnitFloat(turn.z));
    const double a = std::sqrt(1.0 - u1);
    const double b = std::sqrt(u1);
    const double qx = a * std::sin(u2);
    const double qy = a * std::cos(u2);
    const double qz = b * std::sin(u3);
    const double qw = b * std::cos(u3);
    return {1.0 - 2.0 * (qy * qy + qz * qz), 2.0 * (qx * qy - qz * qw),
            2.0 * (qx * qz + qy * qw),       2.0 * (qx * qy + qz * qw),
            1.0 - 2.0 * (qx * qx + qz * qz), 2.0 * (qy * qz - qx * qw),
            2.0 * (qx * qz - qy * qw),       2.0 * (qy * qz + qx * qw),
            1.0 - 2.0 * (qx * qx + qy * qy)};
}

/// `PerlinStructureFunction(r) / r^2` at r = 2^(k / 4) for k from `kFirstQuarter`, estimated from
/// 2^26 pairs each.
constexpr int kFirstQuarter = -24;
constexpr std::array<double, 33> kStructureOverSquare{
    0.772366, 0.772241, 0.772129, 0.771929, 0.771444, 0.770975, 0.770202, 0.769434, 0.767832,
    0.766070, 0.763032, 0.759311, 0.753675, 0.745950, 0.735267, 0.720564, 0.700391, 0.673043,
    0.636947, 0.589660, 0.530574, 0.459215, 0.377706, 0.291690, 0.209524, 0.141536, 0.094237,
    0.065236, 0.046153, 0.032397, 0.022947, 0.016231, 0.011443};
constexpr double kLargeLag = 0.183120;

/// `Perlin3StructureFunction(r) / r^2` on the same lags, estimated from 2^24 pairs each.
constexpr std::array<double, 33> kStructure3OverSquare{
    0.494082, 0.494230, 0.494034, 0.493864, 0.493805, 0.493342, 0.493074, 0.492376, 0.491455,
    0.490546, 0.488813, 0.486484, 0.483336, 0.478718, 0.472398, 0.463827, 0.452007, 0.435827,
    0.414506, 0.386695, 0.351648, 0.308521, 0.258949, 0.205715, 0.153652, 0.108458, 0.074433,
    0.051596, 0.036333, 0.025663, 0.018156, 0.012839, 0.009067};
constexpr double kLargeLag3 = 0.145198;

/// A structure function at lag `r` from its `table` of value over r^2, `large` beyond the table.
double Interpolated(const std::array<double, 33>& table, double large, double r) {
    if (!(r > 0.0)) {
        return 0.0;
    }
    const double quarters = 4.0 * std::log2(r) - kFirstQuarter;
    if (quarters <= 0.0) {
        return table.front() * r * r;
    }
    const auto last = static_cast<double>(table.size() - 1);
    if (quarters >= last) {
        return large;
    }
    const auto k = static_cast<std::size_t>(quarters);
    const double t = quarters - static_cast<double>(k);
    return ((1.0 - t) * table[k] + t * table[k + 1]) * r * r;
}

}  // namespace

void ValidateRoughnessOptions(const RoughnessOptions& options) {
    RequirePositive(options.rmsSlope, "Roughness rmsSlope");
    RequirePositive(options.baseline, "Roughness baseline");
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
    return Interpolated(kStructureOverSquare, kLargeLag, r);
}

double Perlin3StructureFunction(double r) {
    return Interpolated(kStructure3OverSquare, kLargeLag3, r);
}

RoughnessLayer::RoughnessLayer(RoughnessOptions options, Surface surface)
    : FormulaLayer(surface), options_(options) {
    ValidateRoughnessOptions(options_);
    const double shortest = options_.minWavelength * (1.0 - 1e-12);
    double wavelength = options_.maxWavelength;
    for (std::uint32_t i = 0; wavelength >= shortest; ++i, wavelength *= 0.5) {
        octaves_.push_back(MakeOctave(i, wavelength));
    }
    const double scale = options_.rmsSlope / RmsSlope(options_.baseline);
    for (Octave& octave : octaves_) {
        octave.amplitude *= scale;
    }
}

RoughnessLayer::Octave RoughnessLayer::MakeOctave(std::uint32_t i, double wavelength) const {
    const UVec3 hash = Pcg3dHash({options_.seed, i, kRoughnessOctaveStream});
    const double angle = kGoldenAngle * static_cast<double>(i);
    Octave octave{wavelength,
                  std::pow(wavelength, options_.hurst),
                  std::cos(angle),
                  std::sin(angle),
                  LatticeOffset(hash.x),
                  LatticeOffset(hash.y),
                  hash.z};
    if (surface_.IsBody()) {
        octave.rotation = UniformRotation(Pcg3dHash({options_.seed, i, kRoughnessRotationStream}));
        octave.offsetZ = LatticeOffset(Pcg3dHash({hash.z, i, kOffsetZSalt}).x);
    }
    return octave;
}

double RoughnessLayer::OctaveNoise(const Octave& octave, double x, double y) const {
    if (surface_.IsBody()) {
        const Vec3d p = surface_.Position({x, y});
        const std::array<double, 9>& m = octave.rotation;
        const double scale = 1.0 / octave.wavelength;
        return static_cast<double>(PerlinNoise3Precise(
            scale * (m[0] * p.x + m[1] * p.y + m[2] * p.z) + octave.offsetX,
            scale * (m[3] * p.x + m[4] * p.y + m[5] * p.z) + octave.offsetY,
            scale * (m[6] * p.x + m[7] * p.y + m[8] * p.z) + octave.offsetZ, octave.seed));
    }
    const double u = (octave.cos * x + octave.sin * y) / octave.wavelength + octave.offsetX;
    const double v = (octave.cos * y - octave.sin * x) / octave.wavelength + octave.offsetY;
    return static_cast<double>(PerlinNoise2Precise(u, v, octave.seed));
}

double RoughnessLayer::RmsSlope(double baseline) const {
    double structure = 0.0;
    for (const Octave& octave : octaves_) {
        const double lag = baseline / octave.wavelength;
        structure +=
            octave.amplitude * octave.amplitude *
            (surface_.IsBody() ? Perlin3StructureFunction(lag) : PerlinStructureFunction(lag));
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

float RoughnessLayer::HeightAt(float height, double x, double y, double spacing,
                               const Ground* /*ground*/) const {
    double sum = 0.0;
    for (const Octave& octave : octaves_) {
        const double weight = ResolutionWeight(octave.wavelength, spacing, options_.fade);
        if (weight <= 0.0) {
            break;
        }
        sum += weight * octave.amplitude * OctaveNoise(octave, x, y);
    }
    return height + static_cast<float>(sum);
}

}  // namespace terraingen
