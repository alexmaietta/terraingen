/// @file
/// @brief Gaskell's implicitly connected quadrilateral (ICQ) shape models: reading, writing and
/// triangulating them.
#pragma once

#include "terraingen/mesh/mesh.h"
#include "terraingen/util/radial_shape.h"

#include <cstddef>
#include <filesystem>
#include <vector>

namespace terraingen {

/// A shape model in Gaskell's implicitly connected quadrilateral (ICQ) format, in meters: one
/// vertex per label (I, J, F) of the cube grid (`RadialShape::LabelDirection`), edge and corner
/// vertices stored once per face as in the files. R. W. Gaskell, "The Implicitly Connected
/// Quadrilateral Format",
/// https://sbnarchive.psi.edu/pds3/dawn/fc/DWNCSPC_4_01/DOCUMENT/ICQMODEL.ASC
struct IcqModel {
    std::size_t q = 0;            ///< Q, the vertex grid's intervals per face edge.
    std::vector<Vec3d> vertices;  ///< 6 (Q + 1)^2 vertices in file order: F, then J, then I.

    /// The index in `vertices` of v(`i`, `j`, `face`) (`IcqLabelIndex`).
    std::size_t Index(std::size_t i, std::size_t j, std::size_t face) const {
        return IcqLabelIndex(q, i, j, face);
    }

    /// Memberwise equality.
    bool operator==(const IcqModel& other) const {
        return q == other.q && vertices == other.vertices;
    }
    bool operator!=(const IcqModel& other) const {
        return !(*this == other);
    }
};

/// Whether `path` ends in .icq, in any case.
bool IsIcqPath(const std::filesystem::path& path);

/// The unit direction of ICQ label (`i`, `j`, `face`), as `RadialShape::LabelDirection`.
/// @throws std::invalid_argument as `RadialShape::LabelDirection`.
inline Vec3d IcqDirection(std::size_t q, std::size_t i, std::size_t j, std::size_t face) {
    return RadialShape::LabelDirection(q, i, j, face);
}

/// Reads an ICQ file: Q on the first line, then 6 (Q + 1)^2 lines of x, y and z (extra columns,
/// such as albedo, are ignored).
/// @param metersPerUnit Meters per file unit; Gaskell's models are in kilometers.
/// @throws std::runtime_error if the file cannot be read, or holds a bad Q, too few vertices or a
/// value that is not a finite number.
IcqModel ReadIcq(const std::filesystem::path& path, double metersPerUnit = 1000.0);

/// Writes `model` as an ICQ file.
/// @param metersPerUnit Meters per file unit; Gaskell's models are in kilometers.
/// @throws std::invalid_argument for a Q of 0, a vertex count other than 6 (Q + 1)^2 or a vertex
/// that is not finite.
/// @throws std::runtime_error if the file cannot be written.
void WriteIcq(const std::filesystem::path& path, const IcqModel& model,
              double metersPerUnit = 1000.0);

/// The model as 12 Q^2 triangles over its 6 Q^2 + 2 distinct vertices, as Gaskell converts it:
/// each shared edge and corner vertex taken from one face, each quadrilateral split along its
/// shorter diagonal, triangles wound outward.
/// @throws std::invalid_argument as `WriteIcq`.
TriangleMesh IcqToMesh(const IcqModel& model);

/// The model as a radial shape of the same Q: each vertex's distance from the origin when every
/// vertex lies along its label's `IcqDirection`, as in the ICQ files TerrainGen writes; otherwise,
/// as in Gaskell's models, whose vertices are relaxed off those directions, the model resampled
/// along rays from the origin (`ShapeModelFromMesh` of `IcqToMesh`).
/// @throws std::invalid_argument as `WriteIcq`.
/// @throws std::runtime_error as `ShapeModelFromMesh`.
RadialShape ToRadialShape(const IcqModel& model);

/// The shape as an ICQ model: each label at its direction times its radius.
IcqModel ToIcq(const RadialShape& shape);

}  // namespace terraingen
