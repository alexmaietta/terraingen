/// @file
/// @brief Default boulder styles, option validation and seeded boulder shape generation.
#include "terraingen/meshes/boulder.h"

#include "terraingen/noise/kernels/hash.h"
#include "terraingen/noise/kernels/perlin.h"
#include "terraingen/random/stream.h"
#include "terraingen/util/checks.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace terraingen {

namespace {

constexpr std::uint32_t kBoulderStream = 0x424F554Cu;

/// Throws unless `low` <= `range.min` <= `range.max` <= `high`.
void CheckRange(const ValueRange& range, double low, double high, const char* what) {
    if (!(range.min >= low && range.min <= range.max && range.max <= high)) {
        throw std::invalid_argument(std::string("Boulder style ") + what + " must satisfy " +
                                    std::to_string(low) +
                                    " <= min <= max <= " + std::to_string(high));
    }
}

double Draw(RandomStream& random, const ValueRange& range) {
    return range.min + (range.max - range.min) * random.NextDouble();
}

/// Fractal 3D Perlin noise at `p`, `octaves` octaves halving in wavelength from `wavelength`, each
/// of half the amplitude, normalized to about [-1, 1].
double Fractal(const Vec3d& p, const BoulderStyle& style, std::uint32_t seed) {
    double sum = 0.0;
    double norm = 0.0;
    double amplitude = 1.0;
    double frequency = 1.0 / style.noiseWavelength;
    for (int octave = 0; octave < style.noiseOctaves; ++octave) {
        sum += amplitude *
               static_cast<double>(PerlinNoise3(static_cast<float>(p.x * frequency + 17.31),
                                                static_cast<float>(p.y * frequency - 5.77),
                                                static_cast<float>(p.z * frequency + 3.13),
                                                seed + static_cast<std::uint32_t>(octave)));
        norm += amplitude;
        amplitude *= 0.5;
        frequency *= 2.0;
    }
    return sum / norm;
}

/// A unit vector, uniform on the sphere when `maxTilt` is 1, with |z| at most `maxTilt`.
Vec3d DrawNormal(RandomStream& random, double maxTilt) {
    const double z = maxTilt * (2.0 * random.NextDouble() - 1.0);
    const double angle = 2.0 * std::numbers::pi * random.NextDouble();
    const double ring = std::sqrt(std::fmax(0.0, 1.0 - z * z));
    return {ring * std::cos(angle), ring * std::sin(angle), z};
}

}  // namespace

std::vector<BoulderStyle> DefaultBoulderStyles() {
    std::vector<BoulderStyle> styles(6);
    styles[0] = {"block", 0.32, BaseSolid::Cube, {0.6, 0.85}, {0.4, 0.6}, {0.0, 0.25},
                 3,       7,    {0.65, 0.92},    1.0,         0.03,       0.5,
                 3};
    styles[1] = {"slab", 0.2, BaseSolid::Cube, {0.6, 0.9}, {0.15, 0.3}, {0.0, 0.2},
                 2,      5,   {0.6, 0.9},      0.3,        0.02,        0.5,
                 3};
    styles[2] = {"rounded",
                 0.2,
                 BaseSolid::Icosahedron,
                 {0.65, 0.9},
                 {0.45, 0.7},
                 {0.6, 1.0},
                 0,
                 3,
                 {0.7, 0.95},
                 1.0,
                 0.1,
                 0.6,
                 3};
    styles[3] = {"wedge", 0.13, BaseSolid::Prism, {0.55, 0.85}, {0.4, 0.6}, {0.0, 0.3},
                 1,       4,    {0.65, 0.92},     1.0,          0.04,       0.5,
                 3};
    styles[4] = {"pyramid", 0.1, BaseSolid::Pyramid, {0.7, 0.95}, {0.45, 0.7}, {0.0, 0.3},
                 1,         3,   {0.7, 0.92},        1.0,         0.04,        0.5,
                 3};
    styles[5] = {"shard",    0.05,       BaseSolid::Tetrahedron,
                 {0.6, 0.9}, {0.4, 0.6}, {0.0, 0.2},
                 1,          3,          {0.7, 0.92},
                 1.0,        0.03,       0.5,
                 3};
    return styles;
}

void ValidateBoulderShapeOptions(const BoulderShapeOptions& options) {
    if (options.styles.empty()) {
        throw std::invalid_argument("Boulder shapes need at least one style");
    }
    double total = 0.0;
    for (const BoulderStyle& style : options.styles) {
        RequireNonNegative(style.weight, "Boulder style weights");
        total += style.weight;
        CheckRange(style.intermediate, 1e-3, 1.0, "intermediate");
        CheckRange(style.shortAxis, 1e-3, 1.0, "shortAxis");
        CheckRange(style.rounding, 0.0, 1.0, "rounding");
        CheckRange(style.fractureDepth, 1e-3, 1.0, "fractureDepth");
        if (!(style.minFractures >= 0 && style.minFractures <= style.maxFractures &&
              style.maxFractures <= 64)) {
            throw std::invalid_argument(
                "Boulder style fractures must satisfy 0 <= min <= max <= 64");
        }
        RequireUnitInterval(style.maxFractureTilt, "Boulder style maxFractureTilt");
        if (!(style.noiseAmplitude >= 0.0 && style.noiseAmplitude < 1.0 &&
              IsPositive(style.noiseWavelength) && style.noiseOctaves > 0 &&
              style.noiseOctaves <= 12)) {
            throw std::invalid_argument("Boulder style noise needs an amplitude in [0, 1), a "
                                        "positive wavelength and 1 to 12 octaves");
        }
    }
    if (!(total > 0.0)) {
        throw std::invalid_argument("Boulder style weights must not all be zero");
    }
    if (options.subdivisions < 0 || options.subdivisions > 6) {
        throw std::invalid_argument("Boulder subdivisions must be between 0 and 6");
    }
}

BoulderShape GenerateBoulderShape(std::uint64_t shapeSeed, const BoulderShapeOptions& options) {
    ValidateBoulderShapeOptions(options);
    RandomStream random(options.seed, kBoulderStream, static_cast<std::uint32_t>(shapeSeed),
                        static_cast<std::uint32_t>(shapeSeed >> 32));
    double total = 0.0;
    for (const BoulderStyle& style : options.styles) {
        total += style.weight;
    }
    double pick = total * random.NextDouble();
    std::size_t index = 0;
    while (index + 1 < options.styles.size() &&
           (options.styles[index].weight == 0.0 || pick >= options.styles[index].weight)) {
        pick -= options.styles[index].weight;
        ++index;
    }
    const BoulderStyle& style = options.styles[index];
    const double intermediate = Draw(random, style.intermediate);
    const double shortAxis = std::fmin(Draw(random, style.shortAxis), intermediate);
    const double rounding = Draw(random, style.rounding);
    const std::uint32_t noiseSeed = random.NextU32();
    const Vec3d axes{1.0, intermediate, shortAxis};

    const TriangleMesh solid = BaseSolidMesh(style.solid);
    const bool sphere = style.solid == BaseSolid::Icosahedron;
    TriangleMesh mesh = Subdivide(solid, options.subdivisions, sphere);
    for (Vec3d& p : mesh.positions) {
        const Vec3d stretched{p.x * axes.x, p.y * axes.y, p.z * axes.z};
        const Vec3d ray = Normalized(stretched);
        const double ellipsoid =
            1.0 / std::sqrt(std::pow(ray.x / axes.x, 2.0) + std::pow(ray.y / axes.y, 2.0) +
                            std::pow(ray.z / axes.z, 2.0));
        const double radius = (1.0 - rounding) * Length(stretched) + rounding * ellipsoid;
        const double rough = 1.0 + style.noiseAmplitude * Fractal(ray, style, noiseSeed);
        p = ray * (radius * rough);
    }

    const int span = style.maxFractures - style.minFractures + 1;
    const int fractures =
        style.minFractures + std::min(span - 1, static_cast<int>(random.NextDouble() * span));
    for (int n = 0; n < fractures; ++n) {
        const Vec3d normal = DrawNormal(random, style.maxFractureTilt);
        const double depth = Draw(random, style.fractureDepth);
        double reach = 0.0;
        for (std::size_t v = 0; v < solid.positions.size(); ++v) {
            reach = std::fmax(reach, Dot(normal, mesh.positions[v]));
        }
        const double plane = depth * reach;
        if (!(plane > 0.0)) {
            continue;
        }
        for (Vec3d& p : mesh.positions) {
            const double along = Dot(normal, p);
            if (along > plane) {
                p = p * (plane / along);
            }
        }
    }

    const Vec3d extents = Bounds(mesh).Extents();
    mesh = Scaled(mesh, {1.0 / extents.x, intermediate / extents.y, shortAxis / extents.z});
    return {std::move(mesh), index, intermediate, shortAxis};
}

std::uint64_t BoulderShapeSeed(std::uint64_t id, std::uint32_t prototypes) {
    if (prototypes == 0) {
        return id;
    }
    const UVec3 hash = Pcg3dHash(
        {static_cast<std::uint32_t>(id), static_cast<std::uint32_t>(id >> 32), 0x50524F54u});
    return hash.x % prototypes;
}

}  // namespace terraingen
