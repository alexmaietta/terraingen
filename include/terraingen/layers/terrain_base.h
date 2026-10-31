// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief `TerrainBase`, the surface and source crater and boulder layers are built on.
#pragma once

#include "terraingen/geometry/surface.h"
#include "terraingen/sources/source.h"

#include <memory>
#include <type_traits>
#include <utility>

namespace terraingen {

/// The surface and source, the same as their `Terrain`'s, that crater, secondary crater and
/// boulder layers derive their cells, caches and completeness from when built.
struct TerrainBase {
    Surface surface{};  ///< What the terrain lies on; a plane by default.
    std::shared_ptr<const Source> source = nullptr;  ///< The starting heights; null for none.

    TerrainBase() = default;
    /// A base on `lying`, with `heights` as its source if given.
    TerrainBase(Surface lying, std::shared_ptr<const Source> heights = nullptr)
        : surface(std::move(lying)), source(std::move(heights)) {}
    /// A base on a plane with `heights` as its source, of any source type.
    template <typename S, typename = std::enable_if_t<std::is_convertible_v<
                              std::shared_ptr<S>, std::shared_ptr<const Source>>>>
    TerrainBase(std::shared_ptr<S> heights) : source(std::move(heights)) {}
};

}  // namespace terraingen
