#include "terraingen/layers/boulders/boulder_shape.h"

#include "terraingen/noise/hash.h"
#include "terraingen/noise/perlin.h"
#include "terraingen/random/stream.h"
#include "terraingen/util/bits.h"
#include "terraingen/util/checks.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace terraingen {

namespace {

/// Throws unless `low` <= `range.min` <= `range.max` <= `high`.
void CheckRange(const ValueRange& range, double low, double high, const char* what) {
    if (!(range.min >= low && range.min <= range.max && range.max <= high)) {
        throw std::invalid_argument(std::string("Boulder style ") + what + " must satisfy " +
                                    std::to_string(low) +
                                    " <= min <= max <= " + std::to_string(high));
    }
}

/// A uniform draw from `range`, from the next uniform of `random`.
double Draw(RandomStream& random, const ValueRange& range) {
    return range.Lerp(random.NextDouble());
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
    const double angle = 2.0 * kPi * random.NextDouble();
    const double ring = std::sqrt(std::fmax(0.0, 1.0 - z * z));
    return {ring * std::cos(angle), ring * std::sin(angle), z};
}

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

/// The index of the style a uniform draw picks, each in proportion to its weight.
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
/// Planes too far beyond the cut so far to move a vertex are passed over unroughened.
void CutSoftly(TriangleMesh& mesh, const std::vector<Fracture>& fractures,
               const BoulderStyle& style, double softness, std::uint32_t seed, bool polyhedral,
               std::uint32_t noiseSeed) {
    const double frequency = 1.5;
    const double blend = softness > 0.0 ? 4.0 * softness : 0.0;
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
            if (std::isfinite(reach) &&
                offset * (1.0 - style.faceRoughness) / along >= (reach + blend) * (1.0 + 1e-9)) {
                continue;
            }
            if (style.faceRoughness > 0.0) {
                const double shift = 7.13 * static_cast<double>(i);
                const Vec3d at{ray.x * frequency + shift, ray.y * frequency - shift,
                               ray.z * frequency + 0.5 * shift};
                offset *=
                    1.0 + style.faceRoughness *
                              SurfaceRoughness(at, style, seed + static_cast<std::uint32_t>(i));
            }
            const double cut = offset / along;
            reach = softness > 0.0 && std::isfinite(reach) ? SmoothMin(reach, cut, blend)
                                                           : std::fmin(reach, cut);
        }
        if (polyhedral) {
            reach *= 1.0 + style.noiseAmplitude * SurfaceRoughness(ray, style, noiseSeed);
        }
        p = ray * reach;
    }
}

/// The next fracture plane of `random` for `style`: a drawn normal, offset by a drawn share of
/// the farthest reach along it of the first `corners` points of `mesh`.
Fracture DrawFracture(RandomStream& random, const BoulderStyle& style, const TriangleMesh& mesh,
                      std::size_t corners) {
    const Vec3d normal = DrawNormal(random, style.maxFractureTilt);
    const double depth = Draw(random, style.fractureDepth);
    double reach = 0.0;
    for (std::size_t v = 0; v < corners; ++v) {
        reach = std::fmax(reach, Dot(normal, mesh.positions[v]));
    }
    return {normal, depth * reach};
}

/// Moves the points of `mesh` beyond `fracture` along their rays onto it.
void CutSharply(TriangleMesh& mesh, const Fracture& fracture) {
    for (Vec3d& p : mesh.positions) {
        const double along = Dot(fracture.normal, p);
        if (along > fracture.offset) {
            p = p * (fracture.offset / along);
        }
    }
}

/// Cuts `mesh` with the style's fracture planes, each at a drawn share of the reach of the solid's
/// original corners. `Subdivide` keeps those corners as the first `corners` vertices.
void ApplyFractures(TriangleMesh& mesh, std::size_t corners, const BoulderStyle& style,
                    RandomStream& random, double softness, std::uint32_t seed,
                    const std::vector<Fracture>& faces, std::uint32_t noiseSeed) {
    const int span = style.maxFractures - style.minFractures + 1;
    const int fractures =
        style.minFractures + std::min(span - 1, static_cast<int>(random.NextDouble() * span));
    const bool soft = softness > 0.0 || style.faceRoughness > 0.0;
    std::vector<Fracture> planes = soft ? faces : std::vector<Fracture>{};
    for (int n = 0; n < fractures; ++n) {
        const Fracture fracture = DrawFracture(random, style, mesh, corners);
        if (!(fracture.offset > 0.0)) {
            continue;
        }
        if (soft) {
            planes.push_back(fracture);
        } else {
            CutSharply(mesh, fracture);
        }
    }
    if (soft) {
        CutSoftly(mesh, planes, style, softness, seed, !faces.empty(), noiseSeed);
    }
}

}  // namespace

std::vector<BoulderStyle> DefaultBoulderStyles() {
    BoulderStyle angular;
    angular.name = "angular";
    angular.weight = 0.2;
    angular.solid = BaseSolid::Icosahedron;
    angular.intermediate = {0.6, 0.9};
    angular.shortAxis = {0.45, 0.7};
    angular.minFractures = 7;
    angular.maxFractures = 12;
    angular.fractureDepth = {0.55, 0.85};
    angular.maxFractureTilt = 1.0;
    angular.noiseAmplitude = 0.06;
    angular.noiseWavelength = 0.9;
    angular.noiseOctaves = 4;
    angular.edgeSoftness = {0.02, 0.06};
    angular.faceRoughness = 0.03;

    BoulderStyle platy;
    platy.name = "platy";
    platy.weight = 0.12;
    platy.solid = BaseSolid::Icosahedron;
    platy.intermediate = {0.6, 0.9};
    platy.shortAxis = {0.22, 0.38};
    platy.minFractures = 5;
    platy.maxFractures = 9;
    platy.fractureDepth = {0.6, 0.88};
    platy.maxFractureTilt = 0.7;
    platy.noiseAmplitude = 0.05;
    platy.noiseWavelength = 0.9;
    platy.noiseOctaves = 4;
    platy.edgeSoftness = {0.02, 0.05};
    platy.faceRoughness = 0.03;

    BoulderStyle subrounded;
    subrounded.name = "subrounded";
    subrounded.weight = 0.15;
    subrounded.solid = BaseSolid::Icosahedron;
    subrounded.intermediate = {0.65, 0.9};
    subrounded.shortAxis = {0.45, 0.7};
    subrounded.minFractures = 4;
    subrounded.maxFractures = 7;
    subrounded.fractureDepth = {0.65, 0.9};
    subrounded.maxFractureTilt = 1.0;
    subrounded.noiseAmplitude = 0.07;
    subrounded.noiseWavelength = 0.8;
    subrounded.noiseOctaves = 4;
    subrounded.edgeSoftness = {0.06, 0.12};
    subrounded.faceRoughness = 0.02;

    BoulderStyle rounded;
    rounded.name = "rounded";
    rounded.weight = 0.1;
    rounded.solid = BaseSolid::Icosahedron;
    rounded.intermediate = {0.7, 0.95};
    rounded.shortAxis = {0.5, 0.75};
    rounded.minFractures = 0;
    rounded.maxFractures = 3;
    rounded.fractureDepth = {0.75, 0.95};
    rounded.maxFractureTilt = 1.0;
    rounded.noiseAmplitude = 0.1;
    rounded.noiseWavelength = 0.7;
    rounded.noiseOctaves = 4;
    rounded.edgeSoftness = {0.1, 0.18};
    rounded.faceRoughness = 0.02;

    BoulderStyle block;
    block.name = "block";
    block.weight = 0.13;
    block.solid = BaseSolid::Cube;
    block.intermediate = {0.6, 0.85};
    block.shortAxis = {0.4, 0.6};
    block.rounding = {0.0, 0.15};
    block.minFractures = 3;
    block.maxFractures = 7;
    block.fractureDepth = {0.6, 0.85};
    block.maxFractureTilt = 1.0;
    block.noiseAmplitude = 0.07;
    block.noiseWavelength = 0.8;
    block.noiseOctaves = 4;
    block.edgeSoftness = {0.06, 0.12};
    block.faceRoughness = 0.06;

    BoulderStyle wedge;
    wedge.name = "wedge";
    wedge.weight = 0.1;
    wedge.solid = BaseSolid::Prism;
    wedge.intermediate = {0.55, 0.85};
    wedge.shortAxis = {0.4, 0.6};
    wedge.rounding = {0.0, 0.2};
    wedge.minFractures = 2;
    wedge.maxFractures = 5;
    wedge.fractureDepth = {0.65, 0.9};
    wedge.maxFractureTilt = 1.0;
    wedge.noiseAmplitude = 0.07;
    wedge.noiseWavelength = 0.8;
    wedge.noiseOctaves = 4;
    wedge.edgeSoftness = {0.06, 0.12};
    wedge.faceRoughness = 0.06;

    BoulderStyle pyramid;
    pyramid.name = "pyramid";
    pyramid.weight = 0.1;
    pyramid.solid = BaseSolid::Pyramid;
    pyramid.intermediate = {0.7, 0.95};
    pyramid.shortAxis = {0.45, 0.7};
    pyramid.rounding = {0.0, 0.2};
    pyramid.minFractures = 2;
    pyramid.maxFractures = 4;
    pyramid.fractureDepth = {0.7, 0.92};
    pyramid.maxFractureTilt = 1.0;
    pyramid.noiseAmplitude = 0.07;
    pyramid.noiseWavelength = 0.8;
    pyramid.noiseOctaves = 4;
    pyramid.edgeSoftness = {0.06, 0.12};
    pyramid.faceRoughness = 0.06;

    BoulderStyle shard;
    shard.name = "shard";
    shard.weight = 0.05;
    shard.solid = BaseSolid::Tetrahedron;
    shard.intermediate = {0.6, 0.9};
    shard.shortAxis = {0.4, 0.6};
    shard.rounding = {0.0, 0.2};
    shard.minFractures = 2;
    shard.maxFractures = 4;
    shard.fractureDepth = {0.7, 0.92};
    shard.maxFractureTilt = 1.0;
    shard.noiseAmplitude = 0.04;
    shard.noiseWavelength = 0.8;
    shard.noiseOctaves = 4;
    shard.edgeSoftness = {0.02, 0.05};
    shard.faceRoughness = 0.03;

    BoulderStyle sphere;
    sphere.name = "sphere";
    sphere.weight = 0.05;
    sphere.solid = BaseSolid::Icosahedron;
    sphere.intermediate = {0.85, 1.0};
    sphere.shortAxis = {0.7, 0.9};
    sphere.minFractures = 0;
    sphere.maxFractures = 1;
    sphere.fractureDepth = {0.8, 0.95};
    sphere.maxFractureTilt = 1.0;
    sphere.noiseAmplitude = 0.08;
    sphere.noiseWavelength = 0.7;
    sphere.noiseOctaves = 4;
    sphere.edgeSoftness = {0.12, 0.2};
    sphere.faceRoughness = 0.02;

    return {angular, platy, subrounded, rounded, block, wedge, pyramid, shard, sphere};
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
    const Vec3d center = (box.min + box.max) * 0.5;
    const bool turned = extents.y > extents.x;
    const double length = std::fmax(extents.x, extents.y);
    BoulderShape shape;
    shape.mesh.triangles = rock.mesh.triangles;
    for (const Vec3d& p : rock.mesh.positions) {
        const Vec3d q = (p - center) * (1.0 / length);
        shape.mesh.positions.push_back(turned ? Vec3d{q.y, -q.x, q.z} : q);
    }
    shape.intermediate = std::fmin(extents.x, extents.y) / length;
    shape.shortAxis = extents.z / length;
    shape.material = rock.material;
    return shape;
}

BoulderShape GenerateBoulderShape(std::uint64_t shapeSeed, const BoulderShapeOptions& options) {
    ValidateBoulderShapeOptions(options);
    RandomStream random(options.seed, kBoulderShapeStream, static_cast<std::uint32_t>(shapeSeed),
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
    ApplyFractures(mesh, solid.positions.size(), style, random, softness,
                   SubstreamSeed(noiseSeed, kBoulderFractureStream), faces, noiseSeed);

    const Vec3d extents = Bounds(mesh).Extents();
    mesh = Scaled(mesh, {1.0 / extents.x, intermediate / extents.y, shortAxis / extents.z});
    return {std::move(mesh), index, intermediate, shortAxis};
}

std::uint64_t BoulderShapeSeed(std::uint64_t id, std::uint32_t prototypes) {
    if (prototypes == 0) {
        return id;
    }
    const UVec3 hash = Pcg3dHash({static_cast<std::uint32_t>(id),
                                  static_cast<std::uint32_t>(id >> 32), kBoulderPrototypeStream});
    return hash.x % prototypes;
}

}  // namespace terraingen
