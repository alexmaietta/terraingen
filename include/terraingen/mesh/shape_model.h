// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief `ShapeModelFromMesh`, a body's shape model mesh as a `RadialShape`.
#pragma once

#include "terraingen/mesh/mesh.h"
#include "terraingen/util/radial_shape.h"

#include <cstddef>

namespace terraingen {

/// The radial shape of `q` intervals per face edge of a closed mesh around its origin, the
/// distance to the outermost triangle along each label's ray; a non-star-shaped mesh logs a
/// warning.
/// @throws std::invalid_argument for a `q` of 0 or a mesh without triangles.
/// @throws std::runtime_error if a ray from the origin misses the mesh.
RadialShape ShapeModelFromMesh(const TriangleMesh& mesh, std::size_t q);

}  // namespace terraingen
