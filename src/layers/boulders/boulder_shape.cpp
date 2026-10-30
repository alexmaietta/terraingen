// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/layers/boulders/boulder_shape.h"

#include "terraingen/layers/boulders/boulder_fields.h"
#include "terraingen/noise/hash.h"
#include "terraingen/noise/perlin.h"
#include "terraingen/random/stream.h"
#include "terraingen/util/bits.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace terraingen {

namespace {

/// Throws unless `low` <= `range.min` <= `range.max` <= `high`.
void CheckStyleRange(const ValueRange& range, double low, double high, const char* what) {
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

/// `rock` in the unit form of `BoulderShape`: centered on its box, turned about z so its longer
/// horizontal side lies along x, and scaled so that side is 1 long.
BoulderShape NormalizedBoulderShape(const SceneMesh& rock) {
    const Vec3d extents = CheckRock(rock.mesh);
    BoulderShape shape = UnitBoulderShape(rock.mesh, Bounds(rock.mesh));
    if (extents.y > extents.x) {
        for (Vec3d& p : shape.mesh.positions) {
            p = {p.y, -p.x, p.z};
        }
    }
    shape.material = rock.material;
    return shape;
}

}  // namespace

std::vector<BoulderStyle> DefaultBoulderStyles() {
    constexpr BaseSolid kIcosahedron = BaseSolid::Icosahedron;
    // Fields in order: name, weight, solid, intermediate, shortAxis, rounding, min and max
    // fractures, fractureDepth, maxFractureTilt, noise amplitude, wavelength and octaves,
    // edgeSoftness, faceRoughness.
    return {
        {"angular",
         0.2,
         kIcosahedron,
         {0.6, 0.9},
         {0.45, 0.7},
         {0.0, 0.0},
         7,
         12,
         {0.55, 0.85},
         1.0,
         0.06,
         0.9,
         4,
         {0.02, 0.06},
         0.03},
        {"platy",
         0.12,
         kIcosahedron,
         {0.6, 0.9},
         {0.22, 0.38},
         {0.0, 0.0},
         5,
         9,
         {0.6, 0.88},
         0.7,
         0.05,
         0.9,
         4,
         {0.02, 0.05},
         0.03},
        {"subrounded",
         0.15,
         kIcosahedron,
         {0.65, 0.9},
         {0.45, 0.7},
         {0.0, 0.0},
         4,
         7,
         {0.65, 0.9},
         1.0,
         0.07,
         0.8,
         4,
         {0.06, 0.12},
         0.02},
        {"rounded",
         0.1,
         kIcosahedron,
         {0.7, 0.95},
         {0.5, 0.75},
         {0.0, 0.0},
         0,
         3,
         {0.75, 0.95},
         1.0,
         0.1,
         0.7,
         4,
         {0.1, 0.18},
         0.02},
        {"block",
         0.13,
         BaseSolid::Cube,
         {0.6, 0.85},
         {0.4, 0.6},
         {0.0, 0.15},
         3,
         7,
         {0.6, 0.85},
         1.0,
         0.07,
         0.8,
         4,
         {0.06, 0.12},
         0.06},
        {"wedge",
         0.1,
         BaseSolid::Prism,
         {0.55, 0.85},
         {0.4, 0.6},
         {0.0, 0.2},
         2,
         5,
         {0.65, 0.9},
         1.0,
         0.07,
         0.8,
         4,
         {0.06, 0.12},
         0.06},
        {"pyramid",
         0.1,
         BaseSolid::Pyramid,
         {0.7, 0.95},
         {0.45, 0.7},
         {0.0, 0.2},
         2,
         4,
         {0.7, 0.92},
         1.0,
         0.07,
         0.8,
         4,
         {0.06, 0.12},
         0.06},
        {"shard",
         0.05,
         BaseSolid::Tetrahedron,
         {0.6, 0.9},
         {0.4, 0.6},
         {0.0, 0.2},
         2,
         4,
         {0.7, 0.92},
         1.0,
         0.04,
         0.8,
         4,
         {0.02, 0.05},
         0.03},
        {"sphere",
         0.05,
         kIcosahedron,
         {0.85, 1.0},
         {0.7, 0.9},
         {0.0, 0.0},
         0,
         1,
         {0.8, 0.95},
         1.0,
         0.08,
         0.7,
         4,
         {0.12, 0.2},
         0.02},
    };
}

void ValidateBoulderShapeOptions(const BoulderShapeOptions& options) {
    CheckRanges(options);
    double total = 0.0;
    for (const BoulderStyle& style : options.styles) {
        total += style.weight;
        CheckStyleRange(style.intermediate, 1e-3, 1.0, "intermediate");
        CheckStyleRange(style.shortAxis, 1e-3, 1.0, "shortAxis");
        CheckStyleRange(style.rounding, 0.0, 1.0, "rounding");
        CheckStyleRange(style.fractureDepth, 1e-3, 1.0, "fractureDepth");
        CheckStyleRange(style.edgeSoftness, 0.0, 0.5, "edgeSoftness");
        if (!(style.minFractures <= style.maxFractures)) {
            throw std::invalid_argument("Boulder style fractures must satisfy min <= max");
        }
    }
    if (!(total > 0.0)) {
        throw std::invalid_argument("Boulder style weights must not all be zero");
    }
    for (const auto& rock : options.library) {
        if (rock == nullptr) {
            throw std::invalid_argument("Boulder shape library meshes must not be null");
        }
        CheckRock(rock->mesh);
    }
}

BoulderShape GenerateBoulderShape(std::uint64_t shapeSeed, const BoulderShapeOptions& options) {
    ValidateBoulderShapeOptions(options);
    const IdWords words = SplitId(shapeSeed);
    RandomStream random(options.seed, kBoulderShapeStream, words.low, words.high);
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

BoulderShape UnitBoulderShape(const TriangleMesh& mesh, const Box3d& box) {
    const Vec3d center = (box.min + box.max) * 0.5;
    const Vec3d extents = box.max - box.min;
    const double length = std::fmax(extents.x, extents.y);
    BoulderShape shape;
    shape.mesh.triangles = mesh.triangles;
    for (const Vec3d& p : mesh.positions) {
        shape.mesh.positions.push_back((p - center) * (1.0 / length));
    }
    shape.intermediate = std::fmin(extents.x, extents.y) / length;
    shape.shortAxis = extents.z / length;
    return shape;
}

std::uint64_t BoulderShapeSeed(std::uint64_t id, std::uint32_t prototypes) {
    if (prototypes == 0) {
        return id;
    }
    const IdWords words = SplitId(id);
    const UVec3 hash = Pcg3dHash({words.low, words.high, kBoulderPrototypeStream});
    return hash.x % prototypes;
}

}  // namespace terraingen
