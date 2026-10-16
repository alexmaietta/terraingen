#include "terraingen/layers/boulders/boulder_shape.h"

#include "terraingen/noise/hash.h"
#include "terraingen/noise/perlin.h"
#include "terraingen/random/stream.h"
#include "terraingen/util/checks.h"

#include <algorithm>
#include <cmath>
#include <limits>
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
/// The smooth minimum of `a` and `b` over `k`: their minimum, eased by up to `k` / 4 where they
/// are within `k` of each other.
double SmoothMin(double a, double b, double k) {
    const double h = std::fmax(k - std::fabs(a - b), 0.0) / k;
    return std::fmin(a, b) - 0.25 * k * h * h;
}

/// A fracture plane: points beyond `offset` along `normal` are cut away.
struct Fracture {
    Vec3d normal;
    double offset;
};

/// The planes of `solid`'s faces once stretched to `axes`, one per face.
std::vector<Fracture> SolidFaces(const TriangleMesh& solid, const Vec3d& axes) {
    std::vector<Fracture> faces;
    for (const auto& t : solid.triangles) {
        const Vec3d& a = solid.positions[t[0]];
        const Vec3d normal =
            Normalized(Cross(solid.positions[t[1]] - a, solid.positions[t[2]] - a));
        const Vec3d stretched{normal.x / axes.x, normal.y / axes.y, normal.z / axes.z};
        const double length = Length(stretched);
        const Fracture face{stretched * (1.0 / length), Dot(normal, a) / length};
        const bool seen = std::any_of(faces.begin(), faces.end(), [&](const Fracture& f) {
            return Dot(f.normal, face.normal) > 1.0 - 1e-9 &&
                   std::fabs(f.offset - face.offset) < 1e-9;
        });
        if (!seen) {
            faces.push_back(face);
        }
    }
    return faces;
}

/// Cuts `mesh` with `fractures`, each vertex moving along its ray from the origin to the nearest
/// plane: rippled by the style's face roughness and met through a smooth minimum over `softness`,
/// so faces undulate and edges are worn round. With `polyhedral`, the planes include the solid's
/// own faces, which then bound every ray, roughened by the style's noise of `noiseSeed`.
void CutSoftly(TriangleMesh& mesh, const std::vector<Fracture>& fractures,
               const BoulderStyle& style, double softness, std::uint32_t seed, bool polyhedral,
               std::uint32_t noiseSeed) {
    const double frequency = 1.5;
    for (Vec3d& p : mesh.positions) {
        const double radius = Length(p);
        if (!(radius > 0.0)) {
            continue;
        }
        const Vec3d ray = p * (1.0 / radius);
        double reach = polyhedral ? std::numeric_limits<double>::infinity() : radius;
        for (std::size_t i = 0; i < fractures.size(); ++i) {
            const double along = Dot(fractures[i].normal, ray);
            if (!(along > 0.0)) {
                continue;
            }
            double offset = fractures[i].offset;
            if (style.faceRoughness > 0.0) {
                const double shift = 7.13 * static_cast<double>(i);
                const Vec3d at{ray.x * frequency + shift, ray.y * frequency - shift,
                               ray.z * frequency + 0.5 * shift};
                offset *=
                    1.0 + style.faceRoughness *
                              SurfaceRoughness(at, style, seed + static_cast<std::uint32_t>(i));
            }
            const double cut = offset / along;
            reach = softness > 0.0 && std::isfinite(reach) ? SmoothMin(reach, cut, 4.0 * softness)
                                                           : std::fmin(reach, cut);
        }
        if (polyhedral) {
            reach *= 1.0 + style.noiseAmplitude * SurfaceRoughness(ray, style, noiseSeed);
        }
        p = ray * reach;
    }
}

void ApplyFractures(TriangleMesh& mesh, std::size_t corners, const BoulderStyle& style,
                    RandomStream& random, double softness, std::uint32_t seed,
                    const std::vector<Fracture>& faces, std::uint32_t noiseSeed) {
    const int span = style.maxFractures - style.minFractures + 1;
    const int fractures =
        style.minFractures + std::min(span - 1, static_cast<int>(random.NextDouble() * span));
    if (softness > 0.0 || style.faceRoughness > 0.0) {
        std::vector<Fracture> planes = faces;
        for (int n = 0; n < fractures; ++n) {
            const Vec3d normal = DrawNormal(random, style.maxFractureTilt);
            const double depth = Draw(random, style.fractureDepth);
            double reach = 0.0;
            for (std::size_t v = 0; v < corners; ++v) {
                reach = std::fmax(reach, Dot(normal, mesh.positions[v]));
            }
            if (depth * reach > 0.0) {
                planes.push_back({normal, depth * reach});
            }
        }
        CutSoftly(mesh, planes, style, softness, seed, !faces.empty(), noiseSeed);
        return;
    }
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
        {.name = "angular",
         .weight = 0.2,
         .solid = BaseSolid::Icosahedron,
         .intermediate = {0.6, 0.9},
         .shortAxis = {0.45, 0.7},
         .minFractures = 7,
         .maxFractures = 12,
         .fractureDepth = {0.55, 0.85},
         .maxFractureTilt = 1.0,
         .noiseAmplitude = 0.06,
         .noiseWavelength = 0.9,
         .noiseOctaves = 4,
         .edgeSoftness = {0.02, 0.06},
         .faceRoughness = 0.03},
        {.name = "platy",
         .weight = 0.12,
         .solid = BaseSolid::Icosahedron,
         .intermediate = {0.6, 0.9},
         .shortAxis = {0.22, 0.38},
         .minFractures = 5,
         .maxFractures = 9,
         .fractureDepth = {0.6, 0.88},
         .maxFractureTilt = 0.7,
         .noiseAmplitude = 0.05,
         .noiseWavelength = 0.9,
         .noiseOctaves = 4,
         .edgeSoftness = {0.02, 0.05},
         .faceRoughness = 0.03},
        {.name = "subrounded",
         .weight = 0.15,
         .solid = BaseSolid::Icosahedron,
         .intermediate = {0.65, 0.9},
         .shortAxis = {0.45, 0.7},
         .minFractures = 4,
         .maxFractures = 7,
         .fractureDepth = {0.65, 0.9},
         .maxFractureTilt = 1.0,
         .noiseAmplitude = 0.07,
         .noiseWavelength = 0.8,
         .noiseOctaves = 4,
         .edgeSoftness = {0.06, 0.12},
         .faceRoughness = 0.02},
        {.name = "rounded",
         .weight = 0.1,
         .solid = BaseSolid::Icosahedron,
         .intermediate = {0.7, 0.95},
         .shortAxis = {0.5, 0.75},
         .minFractures = 0,
         .maxFractures = 3,
         .fractureDepth = {0.75, 0.95},
         .maxFractureTilt = 1.0,
         .noiseAmplitude = 0.1,
         .noiseWavelength = 0.7,
         .noiseOctaves = 4,
         .edgeSoftness = {0.1, 0.18},
         .faceRoughness = 0.02},
        {.name = "block",
         .weight = 0.13,
         .solid = BaseSolid::Cube,
         .intermediate = {0.6, 0.85},
         .shortAxis = {0.4, 0.6},
         .rounding = {0.0, 0.15},
         .minFractures = 3,
         .maxFractures = 7,
         .fractureDepth = {0.6, 0.85},
         .maxFractureTilt = 1.0,
         .noiseAmplitude = 0.07,
         .noiseWavelength = 0.8,
         .noiseOctaves = 4,
         .edgeSoftness = {0.06, 0.12},
         .faceRoughness = 0.06},
        {.name = "wedge",
         .weight = 0.1,
         .solid = BaseSolid::Prism,
         .intermediate = {0.55, 0.85},
         .shortAxis = {0.4, 0.6},
         .rounding = {0.0, 0.2},
         .minFractures = 2,
         .maxFractures = 5,
         .fractureDepth = {0.65, 0.9},
         .maxFractureTilt = 1.0,
         .noiseAmplitude = 0.07,
         .noiseWavelength = 0.8,
         .noiseOctaves = 4,
         .edgeSoftness = {0.06, 0.12},
         .faceRoughness = 0.06},
        {.name = "pyramid",
         .weight = 0.1,
         .solid = BaseSolid::Pyramid,
         .intermediate = {0.7, 0.95},
         .shortAxis = {0.45, 0.7},
         .rounding = {0.0, 0.2},
         .minFractures = 2,
         .maxFractures = 4,
         .fractureDepth = {0.7, 0.92},
         .maxFractureTilt = 1.0,
         .noiseAmplitude = 0.07,
         .noiseWavelength = 0.8,
         .noiseOctaves = 4,
         .edgeSoftness = {0.06, 0.12},
         .faceRoughness = 0.06},
        {.name = "shard",
         .weight = 0.05,
         .solid = BaseSolid::Tetrahedron,
         .intermediate = {0.6, 0.9},
         .shortAxis = {0.4, 0.6},
         .rounding = {0.0, 0.2},
         .minFractures = 2,
         .maxFractures = 4,
         .fractureDepth = {0.7, 0.92},
         .maxFractureTilt = 1.0,
         .noiseAmplitude = 0.04,
         .noiseWavelength = 0.8,
         .noiseOctaves = 4,
         .edgeSoftness = {0.02, 0.05},
         .faceRoughness = 0.03},
        {.name = "sphere",
         .weight = 0.05,
         .solid = BaseSolid::Icosahedron,
         .intermediate = {0.85, 1.0},
         .shortAxis = {0.7, 0.9},
         .minFractures = 0,
         .maxFractures = 1,
         .fractureDepth = {0.8, 0.95},
         .maxFractureTilt = 1.0,
         .noiseAmplitude = 0.08,
         .noiseWavelength = 0.7,
         .noiseOctaves = 4,
         .edgeSoftness = {0.12, 0.2},
         .faceRoughness = 0.02},
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
        CheckRange(style.edgeSoftness, 0.0, 0.5, "edgeSoftness");
        if (!(style.faceRoughness >= 0.0 && style.faceRoughness <= 0.5)) {
            throw std::invalid_argument("Boulder style faceRoughness must be in [0, 0.5]");
        }
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
    RequireNonNegative(options.weathering, "Boulder weathering");
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
    const double softness =
        style.edgeSoftness.max > 0.0 ? Draw(random, style.edgeSoftness) * options.weathering : 0.0;
    const Vec3d axes{1.0, intermediate, shortAxis};

    const TriangleMesh solid = BaseSolidMesh(style.solid);
    TriangleMesh mesh =
        Subdivide(solid, options.subdivisions, style.solid == BaseSolid::Icosahedron);
    for (Vec3d& p : mesh.positions) {
        p = ShapeVertex(p, axes, rounding, style, noiseSeed);
    }
    const bool soft = softness > 0.0 || style.faceRoughness > 0.0;
    const std::vector<Fracture> faces = soft && style.solid != BaseSolid::Icosahedron
                                            ? SolidFaces(solid, axes)
                                            : std::vector<Fracture>{};
    ApplyFractures(mesh, solid.positions.size(), style, random, softness, noiseSeed ^ 0x43555453u,
                   faces, noiseSeed);

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
