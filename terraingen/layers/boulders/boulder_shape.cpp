#include "terraingen/layers/boulders/boulder_shape.h"

#include "terraingen/noise/hash.h"
#include "terraingen/noise/perlin.h"
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

/// The surface roughness of a style at direction `p`: `noiseOctaves` octaves of 3D Perlin noise
/// halving in wavelength from `noiseWavelength`, each of half the amplitude, in about [-1, 1].
double SurfaceRoughness(const Vec3d& p, const BoulderStyle& style, std::uint32_t seed) {
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

/// The index of the style a uniform draw picks, each in proportion to its weight.
/// The extents of a library rock, which must be a valid mesh with depth along every axis.
Vec3d CheckRock(const TriangleMesh& mesh) {
    ValidateMesh(mesh);
    const Vec3d extents = Bounds(mesh).Extents();
    if (!(extents.x > 0.0 && extents.y > 0.0 && extents.z > 0.0)) {
        throw std::invalid_argument(
            "Boulder shape library meshes must have depth along every axis");
    }
    return extents;
}

std::size_t PickStyle(RandomStream& random, const std::vector<BoulderStyle>& styles) {
    double total = 0.0;
    for (const BoulderStyle& style : styles) {
        total += style.weight;
    }
    double pick = total * random.NextDouble();
    std::size_t index = 0;
    while (index + 1 < styles.size() &&
           (styles[index].weight == 0.0 || pick >= styles[index].weight)) {
        pick -= styles[index].weight;
        ++index;
    }
    return index;
}

/// The solid's vertex `p` stretched to `axes`, pulled `rounding` of the way to the ellipsoid of
/// those axes and roughened by the style's noise.
Vec3d ShapeVertex(const Vec3d& p, const Vec3d& axes, double rounding, const BoulderStyle& style,
                  std::uint32_t noiseSeed) {
    const Vec3d stretched{p.x * axes.x, p.y * axes.y, p.z * axes.z};
    const Vec3d ray = Normalized(stretched);
    const double ellipsoid =
        1.0 / std::sqrt(std::pow(ray.x / axes.x, 2.0) + std::pow(ray.y / axes.y, 2.0) +
                        std::pow(ray.z / axes.z, 2.0));
    const double radius = (1.0 - rounding) * Length(stretched) + rounding * ellipsoid;
    const double rough = 1.0 + style.noiseAmplitude * SurfaceRoughness(ray, style, noiseSeed);
    return ray * (radius * rough);
}

/// Cuts `mesh` with the style's fracture planes, each at a drawn share of the reach of the solid's
/// original corners. `Subdivide` keeps those corners as the first `corners` vertices.
void ApplyFractures(TriangleMesh& mesh, std::size_t corners, const BoulderStyle& style,
                    RandomStream& random) {
    const int span = style.maxFractures - style.minFractures + 1;
    const int fractures =
        style.minFractures + std::min(span - 1, static_cast<int>(random.NextDouble() * span));
    for (int n = 0; n < fractures; ++n) {
        const Vec3d normal = DrawNormal(random, style.maxFractureTilt);
        const double depth = Draw(random, style.fractureDepth);
        double reach = 0.0;
        for (std::size_t v = 0; v < corners; ++v) {
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
}

}  // namespace

std::vector<BoulderStyle> DefaultBoulderStyles() {
    return {
        {.name = "block",
         .weight = 0.32,
         .solid = BaseSolid::Cube,
         .intermediate = {0.6, 0.85},
         .shortAxis = {0.4, 0.6},
         .rounding = {0.0, 0.25},
         .minFractures = 3,
         .maxFractures = 7,
         .fractureDepth = {0.65, 0.92},
         .maxFractureTilt = 1.0,
         .noiseAmplitude = 0.03,
         .noiseWavelength = 0.5,
         .noiseOctaves = 3},
        {.name = "slab",
         .weight = 0.2,
         .solid = BaseSolid::Cube,
         .intermediate = {0.6, 0.9},
         .shortAxis = {0.15, 0.3},
         .rounding = {0.0, 0.2},
         .minFractures = 2,
         .maxFractures = 5,
         .fractureDepth = {0.6, 0.9},
         .maxFractureTilt = 0.3,
         .noiseAmplitude = 0.02,
         .noiseWavelength = 0.5,
         .noiseOctaves = 3},
        {.name = "rounded",
         .weight = 0.2,
         .solid = BaseSolid::Icosahedron,
         .intermediate = {0.65, 0.9},
         .shortAxis = {0.45, 0.7},
         .rounding = {0.6, 1.0},
         .minFractures = 0,
         .maxFractures = 3,
         .fractureDepth = {0.7, 0.95},
         .maxFractureTilt = 1.0,
         .noiseAmplitude = 0.1,
         .noiseWavelength = 0.6,
         .noiseOctaves = 3},
        {.name = "wedge",
         .weight = 0.13,
         .solid = BaseSolid::Prism,
         .intermediate = {0.55, 0.85},
         .shortAxis = {0.4, 0.6},
         .rounding = {0.0, 0.3},
         .minFractures = 1,
         .maxFractures = 4,
         .fractureDepth = {0.65, 0.92},
         .maxFractureTilt = 1.0,
         .noiseAmplitude = 0.04,
         .noiseWavelength = 0.5,
         .noiseOctaves = 3},
        {.name = "pyramid",
         .weight = 0.1,
         .solid = BaseSolid::Pyramid,
         .intermediate = {0.7, 0.95},
         .shortAxis = {0.45, 0.7},
         .rounding = {0.0, 0.3},
         .minFractures = 1,
         .maxFractures = 3,
         .fractureDepth = {0.7, 0.92},
         .maxFractureTilt = 1.0,
         .noiseAmplitude = 0.04,
         .noiseWavelength = 0.5,
         .noiseOctaves = 3},
        {.name = "shard",
         .weight = 0.05,
         .solid = BaseSolid::Tetrahedron,
         .intermediate = {0.6, 0.9},
         .shortAxis = {0.4, 0.6},
         .rounding = {0.0, 0.2},
         .minFractures = 1,
         .maxFractures = 3,
         .fractureDepth = {0.7, 0.92},
         .maxFractureTilt = 1.0,
         .noiseAmplitude = 0.03,
         .noiseWavelength = 0.5,
         .noiseOctaves = 3},
    };
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
    for (const auto& rock : options.library) {
        if (rock == nullptr) {
            throw std::invalid_argument("Boulder shape library meshes must not be null");
        }
        CheckRock(rock->mesh);
    }
}

BoulderShape NormalizedBoulderShape(const SceneMesh& rock) {
    const Vec3d extents = CheckRock(rock.mesh);
    const Box3d box = Bounds(rock.mesh);
    const Vec3d centre = (box.min + box.max) * 0.5;
    const bool turned = extents.y > extents.x;
    const double length = std::fmax(extents.x, extents.y);
    BoulderShape shape;
    shape.mesh.triangles = rock.mesh.triangles;
    for (const Vec3d& p : rock.mesh.positions) {
        const Vec3d q = (p - centre) * (1.0 / length);
        shape.mesh.positions.push_back(turned ? Vec3d{q.y, -q.x, q.z} : q);
    }
    shape.intermediate = std::fmin(extents.x, extents.y) / length;
    shape.shortAxis = extents.z / length;
    shape.material = rock.material;
    return shape;
}

BoulderShape GenerateBoulderShape(std::uint64_t shapeSeed, const BoulderShapeOptions& options) {
    ValidateBoulderShapeOptions(options);
    RandomStream random(options.seed, kBoulderStream, static_cast<std::uint32_t>(shapeSeed),
                        static_cast<std::uint32_t>(shapeSeed >> 32));
    if (!options.library.empty()) {
        const std::size_t index = random.NextU32() % options.library.size();
        BoulderShape shape = NormalizedBoulderShape(*options.library[index]);
        shape.style = index;
        return shape;
    }
    const std::size_t index = PickStyle(random, options.styles);
    const BoulderStyle& style = options.styles[index];
    const double intermediate = Draw(random, style.intermediate);
    const double shortAxis = std::fmin(Draw(random, style.shortAxis), intermediate);
    const double rounding = Draw(random, style.rounding);
    const std::uint32_t noiseSeed = random.NextU32();
    const Vec3d axes{1.0, intermediate, shortAxis};

    const TriangleMesh solid = BaseSolidMesh(style.solid);
    TriangleMesh mesh =
        Subdivide(solid, options.subdivisions, style.solid == BaseSolid::Icosahedron);
    for (Vec3d& p : mesh.positions) {
        p = ShapeVertex(p, axes, rounding, style, noiseSeed);
    }
    ApplyFractures(mesh, solid.positions.size(), style, random);

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
