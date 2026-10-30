// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief `TerrainBase`, the surface and source crater and boulder layers are built on.
#pragma once

#include "terraingen/geometry/geometry.h"
#include "terraingen/geometry/surface.h"
#include "terraingen/sources/source.h"

#include <memory>
#include <type_traits>
#include <utility>

namespace terraingen {

/// What crater, secondary crater and boulder layers are built on: the surface they lie on and
/// the source whose heights they add to, if any, the same as their `Terrain`'s:
///
///     const TerrainBase base{moon, dem};
///     auto craters = std::make_shared<CraterLayer>(models, options, base);
///     auto boulders = std::make_shared<BoulderLayer>(boulderOptions, craters, base);
///
/// Only these take one, because they derive state from it when built: the cells their features
/// are generated in, the inherited craters and the completeness the source's measured data
/// resolves (`source_resolves_px`), and their caches. Every other layer reads the surface and
/// source from its `LayerContext` when it is evaluated.
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

    /// Where noise on a plane is measured from: the source's `ReferencePoint`, or the origin
    /// without a source.
    Vec2d ReferencePoint() const {
        return source ? source->ReferencePoint() : Vec2d{};
    }
};

}  // namespace terraingen
