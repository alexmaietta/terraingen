// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief The body a configuration's terrain lies on: its specs, fields and surface.
#pragma once

#include "config/common.h"

#include "terraingen/geometry/surface.h"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace terraingen {

/// Body "plane": a projected frame in meters, the default.
struct PlaneSurfaceSpec {};

/// Body "sphere": a body such as a planet or moon (`Surface`); grids and positions are longitude
/// and latitude in degrees, and features are generated over the whole body.
struct SphereSurfaceSpec {
    double radius = 0.0;  ///< Meters; must be given.
};

/// Body "ellipsoid": a body shaped as a triaxial ellipsoid (`Surface::Ellipsoid`), with
/// planetocentric longitude and latitude in degrees and heights along the direction from the
/// center.
struct EllipsoidSurfaceSpec {
    std::vector<double> radii;  ///< Semi-axes a, b, c in meters along x, y and z; exactly three.
};

/// Body "shape_model": a star-shaped body from a shape model file (`Surface::ShapeModel`), an ICQ
/// model or a closed mesh around its origin in body-fixed coordinates, as a `RadialShape`.
struct ShapeModelSurfaceSpec {
    std::string path;
    /// Intervals per face edge of the radial grid; unset keeps an ICQ model's own Q, and uses 256
    /// for a mesh.
    std::optional<std::size_t> q{};
    /// Meters per file unit; unset is 1000 for an ICQ model (kilometers) and 1 for a mesh.
    std::optional<double> metersPerUnit{};
};

/// The body a terrain lies on, a plane or a whole body, named in JSON by its "type".
using SurfaceSpec =
    std::variant<PlaneSurfaceSpec, SphereSurfaceSpec, EllipsoidSurfaceSpec, ShapeModelSurfaceSpec>;

/// The cells along a cube face edge a body mesh or shape model may have.
inline FieldRange CubeFaceCellsRange() {
    return Between(1.0, static_cast<double>(kMaxCubeFaceCells));
}

template <> struct SpecName<PlaneSurfaceSpec> {
    static constexpr std::string_view kName = "plane";
    static constexpr std::string_view kDescription =
        "A flat projected frame in meters, for local sites.";
};
template <typename IO> void Fields(IO& /*io*/, PlaneSurfaceSpec& /*spec*/) {}

template <> struct SpecName<SphereSurfaceSpec> {
    static constexpr std::string_view kName = "sphere";
    static constexpr std::string_view kDescription =
        "A whole body such as a planet or moon: positions in longitude and latitude (degrees) and "
        "features generated over the whole body.";
};
template <typename IO> void Fields(IO& io, SphereSurfaceSpec& spec) {
    io("radius_m", spec.radius, "Radius of the body's datum sphere, in meters.", "m",
       Above(0.0).Required());
}

template <> struct SpecName<EllipsoidSurfaceSpec> {
    static constexpr std::string_view kName = "ellipsoid";
    static constexpr std::string_view kDescription =
        "A whole body shaped as a triaxial ellipsoid, such as an elongated moon or asteroid: "
        "planetocentric longitude and latitude (degrees), heights along the direction from the "
        "center, and features generated over the whole body.";
};
template <typename IO> void Fields(IO& io, EllipsoidSurfaceSpec& spec) {
    io("radii_m", spec.radii,
       "Semi-axes a, b and c of the datum ellipsoid in meters, along x (longitude 0), y "
       "(longitude 90) and z (the north pole).",
       "m", Above(0.0).Length(3).Required());
}

template <> struct SpecName<ShapeModelSurfaceSpec> {
    static constexpr std::string_view kName = "shape_model";
    static constexpr std::string_view kDescription =
        "A whole star-shaped body such as an asteroid, from a shape model: planetocentric "
        "longitude and latitude (degrees), heights along the direction from the center, and "
        "features generated over the whole body. The model is resampled along rays from its "
        "origin, keeping the outermost surface where a ray crosses it more than once.";
};
template <typename IO> void Fields(IO& io, ShapeModelSurfaceSpec& spec) {
    io("path", spec.path,
       config::InputPathHelp("Shape model in body-fixed coordinates around its origin: a Gaskell "
                             "ICQ model (.icq) or a closed mesh (.obj, .gltf, .glb or .stl)"),
       "", Required());
    io("q", spec.q,
       "Intervals per cube face edge of the radial grid the model is resampled on; unset keeps "
       "an ICQ model's own Q and uses 256 for a mesh.",
       "", CubeFaceCellsRange());
    io("meters_per_unit", spec.metersPerUnit,
       "Meters per unit of the file's coordinates; unset is 1000 for an ICQ model, whose "
       "coordinates are kilometers, and 1 for a mesh.",
       "m/unit", Above(0.0));
}

/// The `Surface` a body spec describes, its field ranges checked first; a shape model is loaded
/// once per file, modification time, Q and unit, so surfaces built from one spec compare equal.
/// @throws ConfigError at the field of "body" out of its range or empty, or at "body.path" for a
/// shape model that cannot be loaded.
Surface BuildSurface(const SurfaceSpec& spec, const std::filesystem::path& directory = {});

}  // namespace terraingen
