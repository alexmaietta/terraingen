#include "terraingen/layers/boulders/boulder_fragments.h"

#include "terraingen/random/stream.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace terraingen {

namespace {

/// The seed points of `pieces` pieces of a shape whose box is `box`: around the middle, a third
/// of the way out, a little above or below it so the cracks tilt.
std::vector<Vec3d> SeedPoints(const Box3d& box, int pieces, RandomStream& random) {
    const Vec3d centre = (box.min + box.max) * 0.5;
    const Vec3d half = (box.max - box.min) * 0.5;
    const double turn = 2.0 * std::numbers::pi / pieces;
    const double start = turn * random.NextDouble();
    std::vector<Vec3d> seeds;
    for (int k = 0; k < pieces; ++k) {
        const double angle = start + turn * (k + 0.3 * (random.NextDouble() - 0.5));
        const double out = 0.35 + 0.15 * random.NextDouble();
        const double rise = 0.2 * (random.NextDouble() - 0.5);
        seeds.push_back(centre + Vec3d{out * half.x * std::cos(angle),
                                       out * half.y * std::sin(angle), rise * half.z});
    }
    return seeds;
}

/// The seed points of `pieces` slabs of a shape whose box is `box`: spread along one horizontal
/// line through the middle, a little off it and above or below it, so the cracks between them run
/// roughly parallel right through the shape.
std::vector<Vec3d> SlabSeedPoints(const Box3d& box, int pieces, RandomStream& random) {
    const Vec3d centre = (box.min + box.max) * 0.5;
    const Vec3d half = (box.max - box.min) * 0.5;
    const double angle = std::numbers::pi * random.NextDouble();
    const Vec3d along{std::cos(angle), std::sin(angle), 0.0};
    const Vec3d across{-along.y, along.x, 0.0};
    const double reach = half.x * std::fabs(along.x) + half.y * std::fabs(along.y);
    const double side = half.x * std::fabs(across.x) + half.y * std::fabs(across.y);
    const double step = 2.0 * reach / pieces;
    std::vector<Vec3d> seeds;
    for (int k = 0; k < pieces; ++k) {
        const double at = (k - 0.5 * (pieces - 1) + 0.4 * (random.NextDouble() - 0.5)) * step;
        const double off = 0.3 * side * (random.NextDouble() - 0.5);
        const double rise = 0.3 * half.z * (random.NextDouble() - 0.5);
        seeds.push_back(centre + along * at + across * off + Vec3d{0.0, 0.0, rise});
    }
    return seeds;
}

/// `mesh` cut to the cell of seed `k` among `seeds`: every vertex beyond a plane halfway to
/// another seed is moved along its ray from seed `k` onto that plane.
TriangleMesh CutToCell(const TriangleMesh& mesh, const std::vector<Vec3d>& seeds, std::size_t k) {
    TriangleMesh piece = mesh;
    for (std::size_t j = 0; j < seeds.size(); ++j) {
        if (j == k) {
            continue;
        }
        const Vec3d between = seeds[j] - seeds[k];
        const double length = Length(between);
        const Vec3d normal = between * (1.0 / length);
        const double plane = 0.5 * length;
        for (Vec3d& p : piece.positions) {
            const Vec3d ray = p - seeds[k];
            const double along = Dot(normal, ray);
            if (along > plane) {
                p = seeds[k] + ray * (plane / along);
            }
        }
    }
    return piece;
}

}  // namespace

std::vector<BoulderFragment> FragmentBoulderShape(const BoulderShape& parent, int pieces,
                                                  std::uint64_t seed, BoulderCracks cracks) {
    if (pieces < 2 || pieces > 12) {
        throw std::invalid_argument("A boulder breaks into 2 to 12 pieces");
    }
    RandomStream random(static_cast<std::uint32_t>(seed), kBoulderFragmentStream,
                        static_cast<std::uint32_t>(seed >> 32));
    const Box3d box = Bounds(parent.mesh);
    const std::vector<Vec3d> seeds = cracks == BoulderCracks::Slabs
                                         ? SlabSeedPoints(box, pieces, random)
                                         : SeedPoints(box, pieces, random);
    std::vector<BoulderFragment> fragments;
    for (std::size_t k = 0; k < seeds.size(); ++k) {
        const TriangleMesh cut = CutToCell(parent.mesh, seeds, k);
        const Box3d bounds = Bounds(cut);
        const Vec3d extents = bounds.max - bounds.min;
        BoulderFragment fragment;
        fragment.centre = (bounds.min + bounds.max) * 0.5;
        fragment.scale = std::fmax(extents.x, extents.y);
        fragment.shape.mesh.triangles = cut.triangles;
        for (const Vec3d& p : cut.positions) {
            fragment.shape.mesh.positions.push_back((p - fragment.centre) * (1.0 / fragment.scale));
        }
        fragment.shape.style = parent.style;
        fragment.shape.intermediate = std::fmin(extents.x, extents.y) / fragment.scale;
        fragment.shape.shortAxis = extents.z / fragment.scale;
        fragment.shape.material = parent.material;
        fragments.push_back(std::move(fragment));
    }
    return fragments;
}

}  // namespace terraingen
