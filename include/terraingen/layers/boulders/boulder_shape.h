// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief Procedural boulder meshes: styles, shape options and seeded shape generation.
#pragma once

#include "terraingen/mesh/io/mesh_io.h"
#include "terraingen/mesh/mesh.h"
#include "terraingen/mesh/polyhedra.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace terraingen {

/// An interval to draw a value uniformly from.
struct ValueRange {
    double min = 0.0;  ///< Lower end.
    double max = 0.0;  ///< Upper end, >= `min`.

    /// The value `u` of the way from `min` to `max`: a uniform draw for a uniform `u` in [0, 1).
    double Lerp(double u) const {
        return min + (max - min) * u;
    }

    bool operator==(const ValueRange& other) const {
        return min == other.min && max == other.max;
    }
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
    /// How far edges and corners are worn back where faces meet, as weathering rounds them, in
    /// long-axis lengths; 0 keeps them sharp.
    ValueRange edgeSoftness{0.0, 0.0};
    /// How far fracture faces undulate, as a share of each plane's offset; 0 keeps them flat.
    double faceRoughness = 0.0;

    bool operator==(const BoulderStyle& other) const {
        return name == other.name && weight == other.weight && solid == other.solid &&
               intermediate == other.intermediate && shortAxis == other.shortAxis &&
               rounding == other.rounding && minFractures == other.minFractures &&
               maxFractures == other.maxFractures && fractureDepth == other.fractureDepth &&
               maxFractureTilt == other.maxFractureTilt && noiseAmplitude == other.noiseAmplitude &&
               noiseWavelength == other.noiseWavelength && noiseOctaves == other.noiseOctaves &&
               edgeSoftness == other.edgeSoftness && faceRoughness == other.faceRoughness;
    }
};

/// The default mix of boulder styles, every one cut irregularly, weathered at its edges and rough
/// on its faces: polyhedra from a sphere (angular, platy, subrounded) most of all, then blocks from
/// a cube, wedges, pyramids, rounded boulders, shards from a tetrahedron and near-spheres.
std::vector<BoulderStyle> DefaultBoulderStyles();

/// Settings for `GenerateBoulderShape`.
struct BoulderShapeOptions {
    std::vector<BoulderStyle> styles = DefaultBoulderStyles();  ///< Styles to pick from by weight.
    int subdivisions = 3;    ///< Times the solid's triangles are split in four.
    std::uint32_t seed = 0;  ///< Seed shared by every shape.
    /// Scales every style's edge softness: 0 gives sharp edges, 1 the styles' own, more is more
    /// worn.
    double weathering = 1.0;
    /// Rock meshes, such as `ReadMesh` reads, that replace the styles when not empty: each seed
    /// picks one, normalized to the unit form of `BoulderShape` with its z axis kept vertical.
    std::vector<std::shared_ptr<const SceneMesh>> library{};

    bool operator==(const BoulderShapeOptions& other) const {
        return styles == other.styles && subdivisions == other.subdivisions && seed == other.seed &&
               weathering == other.weathering && library == other.library;
    }
};

/// Checks boulder shape options.
/// @throws std::invalid_argument for no styles, zero total weight, any field outside the range
/// its comment gives, or a library mesh that is null, invalid or flat along an axis.
void ValidateBoulderShapeOptions(const BoulderShapeOptions& options);

/// A boulder's shape in unit form, with the origin inside it.
struct BoulderShape {
    TriangleMesh mesh;          ///< Long axis along x, extent exactly 1.
    std::size_t style = 0;      ///< Index into the options' styles, or its library when it has one.
    double intermediate = 1.0;  ///< Extent along y, in long-axis lengths.
    double shortAxis = 1.0;     ///< Extent along z, in long-axis lengths.
    MeshMaterial material{"rock", {0.5, 0.5, 0.5}};  ///< The library mesh's, or plain gray.
};

/// `mesh` in the unit form of `BoulderShape` without turning it: centered on its bounding `box`
/// and scaled so the longer horizontal side of the box is 1 long; the style and material are left
/// as the defaults.
BoulderShape UnitBoulderShape(const TriangleMesh& mesh, const Box3d& box);

/// The shape a seed gives, a pure function of the seed and options; every subdivision level
/// gives the same shape in finer detail.
/// @param shapeSeed E.g. from `BoulderShapeSeed`.
/// @throws std::invalid_argument as `ValidateBoulderShapeOptions`.
BoulderShape GenerateBoulderShape(std::uint64_t shapeSeed, const BoulderShapeOptions& options);

/// The shape seed of the boulder with ID `id`: `id` itself for 0 `prototypes`, otherwise `id`
/// hashed into [0, `prototypes`), so a field can be drawn with a small library of instanced meshes.
std::uint64_t BoulderShapeSeed(std::uint64_t id, std::uint32_t prototypes = 0);

}  // namespace terraingen
