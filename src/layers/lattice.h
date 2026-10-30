// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief Jittered-lattice cell loops (`ForEachLatticeCell`) and their random streams.
#pragma once

#include "terraingen/geometry/cells.h"
#include "terraingen/geometry/geometry.h"
#include "terraingen/geometry/surface.h"
#include "terraingen/random/stream.h"

#include <cmath>
#include <cstdint>
#include <memory>

namespace terraingen {

/// The cells features are generated in on `surface` (`MakeCells`), made once per thread for the
/// surface it last asked for, so layers reading the surface from their context at each point
/// keep their cells and the areas they recall.
inline std::shared_ptr<const SurfaceCells> ThreadCells(const Surface& surface) {
    thread_local Surface last{};
    thread_local std::shared_ptr<const SurfaceCells> cells = MakeCells(last);
    if (!(surface == last)) {
        cells = MakeCells(surface);
        last = surface;
    }
    return cells;
}

/// The random stream of lattice cell (`i`, `j`) under `seed` and `stream`, keyed by all 64 bits of
/// each index.
inline RandomStream LatticeStream(std::uint32_t seed, std::uint32_t stream, std::int64_t i,
                                  std::int64_t j) {
    return RandomStream(seed, stream, static_cast<std::uint32_t>(i), static_cast<std::uint32_t>(j),
                        static_cast<std::uint32_t>((i >> 32) ^ ((j >> 32) << 16)));
}

/// Calls `visit(i, j)` for each cell within `rings` cells of the one holding `p`, on a lattice of
/// cells `cell.x` by `cell.y` across, row `j` by row with `i` ascending in each.
template <typename Visit>
void ForEachLatticeCell(const Vec2d& p, const Vec2d& cell, std::int64_t rings, const Visit& visit) {
    const auto ci = static_cast<std::int64_t>(std::floor(p.x / cell.x));
    const auto cj = static_cast<std::int64_t>(std::floor(p.y / cell.y));
    for (std::int64_t j = cj - rings; j <= cj + rings; ++j) {
        for (std::int64_t i = ci - rings; i <= ci + rings; ++i) {
            visit(i, j);
        }
    }
}

}  // namespace terraingen
