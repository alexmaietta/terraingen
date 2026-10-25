// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief `TerrainBase`, the surface and source every layer of a terrain is built on.
#pragma once

#include "terraingen/sources/source.h"
#include "terraingen/util/geometry.h"
#include "terraingen/util/surface.h"

#include <memory>
#include <type_traits>
#include <utility>

namespace terraingen {

/// What a terrain's layers are built on: the surface they lie on and the source whose heights
/// they add to, if any. Every height and albedo layer takes one as its last argument, the same
/// for each layer of a terrain and for its `TerrainGenerator`:
///
///     const TerrainBase base{moon, dem};
///     auto craters = std::make_shared<CraterLayer>(models, options, base);
///     auto boulders = std::make_shared<BoulderLayer>(boulderOptions, craters, base);
///
/// Layers read the source's measured data to leave out what it already resolves
/// (`source_resolves_px`), and measure noise from its reference point.
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
