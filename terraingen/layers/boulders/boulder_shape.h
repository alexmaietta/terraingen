/// @file
/// @brief Procedural boulder meshes: styles, shape options and seeded shape generation.
#pragma once

#include "terraingen/mesh/mesh.h"
#include "terraingen/mesh/polyhedra.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace terraingen {

/// An interval to draw a value uniformly from.
struct ValueRange {
    double min = 0.0;  ///< Lower end.
    double max = 0.0;  ///< Upper end, >= `min`.

    bool operator==(const ValueRange&) const = default;
};

/// One kind of boulder: the solid it starts from and the ranges its shape is drawn from.
///
/// The solid is subdivided, stretched, rounded, roughened and cut by fracture planes, every vertex
/// moving only along its ray from the origin, so the mesh never folds over itself. Ratios and
/// lengths are in long-axis lengths.
struct BoulderStyle {
    std::string name;     ///< Label, e.g. "block".
    double weight = 1.0;  ///< Relative share of boulders of this style.
    /// The solid the shape starts from.
    BaseSolid solid = BaseSolid::Icosahedron;
    ValueRange intermediate{0.6, 0.85};  ///< b / a, the intermediate axis over the long one.
    ValueRange shortAxis{0.4, 0.6};      ///< c / a, the short (vertical) axis over the long one.
    ValueRange rounding{0.0, 0.0};       ///< Share toward the ellipsoid: 0 keeps corners, 1 is it.
    int minFractures = 0;                ///< Fewest fracture planes.
    int maxFractures = 0;                ///< Most fracture planes.
    ValueRange fractureDepth{0.6, 0.9};  ///< Plane offset as a share of the shape's reach.
    double maxFractureTilt = 1.0;        ///< Largest |z| of a fracture normal, in [0, 1].
    double noiseAmplitude = 0.05;        ///< Largest relative change of the radius by noise.
    double noiseWavelength = 0.6;        ///< Wavelength of the first noise octave.
    int noiseOctaves = 3;                ///< Noise octaves, each half the wavelength.

    bool operator==(const BoulderStyle&) const = default;
};

/// The default mix of boulder styles: blocks and slabs most of all, then rounded boulders,
/// wedges, pyramids and shards.
std::vector<BoulderStyle> DefaultBoulderStyles();

/// Settings for `GenerateBoulderShape`.
struct BoulderShapeOptions {
    std::vector<BoulderStyle> styles = DefaultBoulderStyles();  ///< Styles to pick from by weight.
    int subdivisions = 3;    ///< Times the solid's triangles are split in four.
    std::uint32_t seed = 0;  ///< Seed shared by every shape.

    bool operator==(const BoulderShapeOptions&) const = default;
};

/// Checks boulder shape options.
/// @throws std::invalid_argument for no styles, a style with a negative weight, all weights zero,
/// an empty or inverted range, axis ratios outside (0, 1], rounding or depths outside [0, 1],
/// fracture counts not in 0 <= min <= max <= 64, a tilt outside [0, 1], noise amplitude outside
/// [0, 1), a non-positive wavelength, octaves outside 1 to 12, or subdivisions outside 0 to 6.
void ValidateBoulderShapeOptions(const BoulderShapeOptions& options);

/// A boulder's shape in unit form, with the origin inside it.
struct BoulderShape {
    TriangleMesh mesh;          ///< Long axis along x, extent exactly 1.
    std::size_t style = 0;      ///< Index into the options' styles.
    double intermediate = 1.0;  ///< Extent along y, in long-axis lengths.
    double shortAxis = 1.0;     ///< Extent along z, in long-axis lengths.
};

/// The shape a seed gives, a pure function of the seed and options; every subdivision level
/// gives the same shape in finer detail.
/// @param shapeSeed E.g. from `BoulderShapeSeed`.
/// @throws std::invalid_argument as `ValidateBoulderShapeOptions`.
BoulderShape GenerateBoulderShape(std::uint64_t shapeSeed, const BoulderShapeOptions& options);

/// The shape seed of the boulder with ID `id`: `id` itself for 0 `prototypes`, otherwise `id`
/// hashed into [0, `prototypes`), so a field can be drawn with a small library of instanced meshes.
std::uint64_t BoulderShapeSeed(std::uint64_t id, std::uint32_t prototypes = 0);

}  // namespace terraingen
