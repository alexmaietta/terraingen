// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief `ShapeModelFromMesh`, a body's shape model mesh as a `RadialShape`.
#pragma once

#include "terraingen/mesh/mesh.h"
#include "terraingen/util/radial_shape.h"

#include <cstddef>

namespace terraingen {

/// The radial shape of `q` intervals per face edge of a closed mesh around its origin, such as a
/// body's shape model in body-fixed meters: at each label, the distance along a ray from the
/// origin to the outermost triangle it crosses. A mesh that rays cross more than once is not
/// star-shaped from its origin; its outermost surface is kept and a warning logged.
/// @throws std::invalid_argument for a `q` of 0 or a mesh without triangles.
/// @throws std::runtime_error if a ray from the origin misses the mesh, which then does not
/// enclose its origin.
RadialShape ShapeModelFromMesh(const TriangleMesh& mesh, std::size_t q);

}  // namespace terraingen
