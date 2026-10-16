/// @file
/// @brief Gaskell's implicitly connected quadrilateral (ICQ) shape models: reading, writing and
/// triangulating them.
#pragma once

#include "terraingen/mesh/mesh.h"

#include <cstddef>
#include <filesystem>
#include <vector>

namespace terraingen {

/// A shape model in Gaskell's implicitly connected quadrilateral (ICQ) format, in metres: the
/// vertices v(I, J, F) labelled as grid points on the six faces of a cube, I and J from 0 to Q and
/// F from 1 to 6, the faces centred on +z, -y, -x, +y, +x and -z. Vertices on the cube's edges and
/// corners are stored once per face, as in the files. Format: R. W. Gaskell, "The Implicitly
/// Connected Quadrilateral Format", NASA PDS Small Bodies Node,
/// https://sbnarchive.psi.edu/pds3/dawn/fc/DWNCSPC_4_01/DOCUMENT/ICQMODEL.ASC
struct IcqModel {
    std::size_t q = 0;            ///< Q, the vertex grid's intervals per face edge.
    std::vector<Vec3d> vertices;  ///< 6 (Q + 1)^2 vertices in file order: F, then J, then I.

    /// The index in `vertices` of v(`i`, `j`, `face`), with `face` from 1 to 6.
    std::size_t Index(std::size_t i, std::size_t j, std::size_t face) const {
        return ((face - 1) * (q + 1) + j) * (q + 1) + i;
    }

    bool operator==(const IcqModel&) const = default;
};

/// Whether `path` ends in .icq, in any case.
bool IsIcqPath(const std::filesystem::path& path);

/// The unit direction of label (`i`, `j`, `face`) of a cube with `q` intervals per edge, at
/// equiangular coordinates, with the faces oriented as in Gaskell's models: on face 1 (+z) I runs
/// along +x and J along -y; on faces 2 to 5 (-y, -x, +y, +x) J runs along -z; on face 6 (-z) I runs
/// along +x and J along +y. Labels the format shares between faces get the same direction exactly.
/// @throws std::invalid_argument unless `q` > 0, `i` and `j` <= `q` and 1 <= `face` <= 6.
Vec3d IcqDirection(std::size_t q, std::size_t i, std::size_t j, std::size_t face);

/// Reads an ICQ file: Q on the first line, then 6 (Q + 1)^2 lines of x, y and z (extra columns,
/// such as albedo, are ignored).
/// @param metresPerUnit Metres per file unit; Gaskell's models are in kilometres.
/// @throws std::runtime_error if the file cannot be read, or holds a bad Q, too few vertices or a
/// value that is not a finite number.
IcqModel ReadIcq(const std::filesystem::path& path, double metresPerUnit = 1000.0);

/// Writes `model` as an ICQ file.
/// @param metresPerUnit Metres per file unit; Gaskell's models are in kilometres.
/// @throws std::invalid_argument for a Q of 0, a vertex count other than 6 (Q + 1)^2 or a vertex
/// that is not finite.
/// @throws std::runtime_error if the file cannot be written.
void WriteIcq(const std::filesystem::path& path, const IcqModel& model,
              double metresPerUnit = 1000.0);

/// The model as 12 Q^2 triangles over its 6 Q^2 + 2 distinct vertices, as Gaskell converts it:
/// each shared edge and corner vertex taken from one face, each quadrilateral split along its
/// shorter diagonal, triangles wound outward.
/// @throws std::invalid_argument as `WriteIcq`.
TriangleMesh IcqToMesh(const IcqModel& model);

}  // namespace terraingen
