#pragma once

#include "terraingen/meshes/mesh.h"
#include "terraingen/meshes/polyhedra.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace terraingen {

/// An interval to draw a value uniformly from.
struct ValueRange {
    double min = 0.0;
    double max = 0.0;

    bool operator==(const ValueRange&) const = default;
};

/// One kind of boulder: the solid it starts from and the ranges its shape is drawn from.
///
/// A shape is built from `solid` subdivided, every vertex moved only along its ray from the origin:
/// 1. stretched to axes 1 : b : c, b from `intermediate` and c from `shortAxis`;
/// 2. rounded toward the ellipsoid of those axes by a share drawn from `rounding` (0 keeps the
///    solid's edges and corners, 1 is the ellipsoid);
/// 3. roughened by 3D Perlin noise (`PerlinNoise3`), `noiseOctaves` octaves halving in wavelength
///    from `noiseWavelength` (in long-axis lengths), changing the radius by up to `noiseAmplitude`;
/// 4. cut by `fractures` planes, flat fracture faces at a share drawn from `fractureDepth` of the
///    shape's reach along each plane's normal, whose normals lie within `maxFractureTilt` (0 to 1,
///    the largest |z| of the unit normal) of horizontal, so a slab is cut about its rim only.
/// Because the solid is convex about the origin and every step moves vertices along their rays, the
/// mesh can never fold over itself.
struct BoulderStyle {
    std::string name;
    double weight = 1.0;  ///< Relative share of boulders of this style.
    BaseSolid solid = BaseSolid::Icosahedron;
    ValueRange intermediate{0.6, 0.85};  ///< b / a, the intermediate axis over the long one.
    ValueRange shortAxis{0.4, 0.6};      ///< c / a, the short (vertical) axis over the long one.
    ValueRange rounding{0.0, 0.0};
    int minFractures = 0;
    int maxFractures = 0;
    ValueRange fractureDepth{0.6, 0.9};
    double maxFractureTilt = 1.0;
    double noiseAmplitude = 0.05;
    double noiseWavelength = 0.6;
    int noiseOctaves = 3;

    bool operator==(const BoulderStyle&) const = default;
};

/// The default mix of boulder styles: blocks and slabs of broken rock most of all, then rounded,
/// weathered boulders, wedges, pyramids and shards, so that a field reads as broken rock rather
/// than as pebbles. The mix, the rounding, the noise and the fractures are chosen for their look,
/// after Chrono::Planet's rock library; the axis ratios are centred on the mean shape of impact
/// fragments, a : b : c = 2 : sqrt(2) : 1, which surveys of lunar and asteroid boulders also find.
///
/// References:
/// - A. Fujiwara, G. Kamimoto and A. Tsukamoto, "Expected shape distribution of asteroids obtained
///   from laboratory impact experiments", Nature 272:602-603, 1978.
///   https://doi.org/10.1038/272602a0
/// - T. Michikami et al., "Fragment shapes in impact experiments ranging from cratering to
///   catastrophic disruption", Icarus 264:316-330, 2016.
///   https://doi.org/10.1016/j.icarus.2015.09.038
std::vector<BoulderStyle> DefaultBoulderStyles();

/// Settings for `GenerateBoulderShape`.
struct BoulderShapeOptions {
    std::vector<BoulderStyle> styles = DefaultBoulderStyles();
    int subdivisions = 3;  ///< Times the solid's triangles are split in four.
    std::uint32_t seed = 0;

    bool operator==(const BoulderShapeOptions&) const = default;
};

/// @throws std::invalid_argument for no styles, a style with a negative weight, all weights zero,
/// an empty or inverted range, axis ratios outside (0, 1], rounding or depths outside [0, 1],
/// fracture counts not in 0 <= min <= max <= 64, a tilt outside [0, 1], negative noise or a
/// non-positive wavelength or octave count, or subdivisions outside 0 to 6.
void ValidateBoulderShapeOptions(const BoulderShapeOptions& options);

/// A boulder's shape in unit form: its long axis along x with an extent of exactly 1, its
/// intermediate axis along y with an extent of `intermediate`, and its short axis along z with an
/// extent of `shortAxis`; the origin lies inside it.
struct BoulderShape {
    TriangleMesh mesh;
    std::size_t style = 0;  ///< Index into the options' styles.
    double intermediate = 1.0;
    double shortAxis = 1.0;
};

/// The shape `shapeSeed` gives under `options`: a pure function of both, so any boulder's mesh can
/// be made again from its seed, and at every subdivision level the same shape in finer detail. The
/// fracture planes are fitted to the solid's original corners, which come first at every level.
/// @throws std::invalid_argument for invalid options.
BoulderShape GenerateBoulderShape(std::uint64_t shapeSeed, const BoulderShapeOptions& options);

/// The shape seed of boulder `id`: the ID itself, so every boulder has a shape of its own, or with
/// `prototypes` above 0 one of that many shared shapes, `id` hashed into [0, prototypes), so a
/// field can be drawn with a small library of instanced meshes.
std::uint64_t BoulderShapeSeed(std::uint64_t id, std::uint32_t prototypes = 0);

}  // namespace terraingen
