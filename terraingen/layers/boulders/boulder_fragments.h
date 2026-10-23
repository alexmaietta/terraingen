/// @file
/// @brief Boulder shapes split into pieces that fit back together.
#pragma once

#include "terraingen/layers/boulders/boulder_shape.h"
#include "terraingen/mesh/mesh.h"

#include <cstdint>
#include <vector>

namespace terraingen {

/// How the cracks of a split boulder run.
enum class BoulderCracks {
    Radial,  ///< Out from the middle, like wedges of a pie.
    Slabs,   ///< Roughly parallel, right through it, like a block split into slabs.
};

/// One piece of a split boulder shape.
struct BoulderFragment {
    /// The piece as `(parent point - center) / scale`: the unit form of `BoulderShape` scaled to
    /// its largest horizontal extent rather than turned to its long axis, so that placing it with
    /// its parent's pose puts it back where it was.
    BoulderShape shape;
    Vec3d center;        ///< Centre of the piece's box in the parent's unit form.
    double scale = 1.0;  ///< The piece's largest horizontal extent in the parent's unit form.
};

/// `parent` split into `pieces` pieces by near-vertical cracks running as `cracks` says, a pure
/// function of the seed.
///
/// Seed points are drawn around the parent's middle, or along a line through it for slabs; each
/// piece is the parent cut by the planes halfway to the other seed points, its vertices moving
/// along their rays from its own seed point, so neighbouring pieces meet on the same plane.
/// @throws std::invalid_argument for fewer than 2 pieces or more than 12.
std::vector<BoulderFragment> FragmentBoulderShape(const BoulderShape& parent, int pieces,
                                                  std::uint64_t seed,
                                                  BoulderCracks cracks = BoulderCracks::Radial);

}  // namespace terraingen
