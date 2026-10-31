// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief `Layer`, relief added on top of a source, and `FormulaLayer` for point-wise layers.
#pragma once

#include "terraingen/geometry/geometry.h"
#include "terraingen/geometry/surface.h"
#include "terraingen/gpu/backend.h"
#include "terraingen/layers/ground.h"
#include "terraingen/layers/terrain_base.h"
#include "terraingen/raster/raster.h"
#include "terraingen/sources/sample_block.h"
#include "terraingen/sources/source.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace terraingen {

class EvaluationCache;

/// How a layer or an albedo is evaluated: where, at what spacing, and on what terrain, as
/// `Ground::Context` gives it.
struct LayerContext {
    /// Sample spacing in world units; features unresolvable at it are left out. At least the cell
    /// size in `Layer::Apply`; positive in `Layer::ApplyAt`.
    double spacing = 0.0;
    unsigned threads = 1;  ///< Worker threads, 0 for all; the result does not depend on it.
    const Ground* ground = nullptr;  ///< The terrain beneath; null where none is needed.
    /// Where grids are evaluated, already resolved to `Cpu` or `Vulkan`. Layers whose relief is a
    /// kernel follow it; other layers, and every point query, run on the CPU.
    Backend backend = Backend::Cpu;
    /// Values the batches of one evaluation of points share, such as crater datums; null for none.
    EvaluationCache* cache = nullptr;
    const Surface* surface = nullptr;  ///< What the terrain lies on; null for a plane.
    const Source* source = nullptr;    ///< The terrain's starting heights; null for none.

    /// The surface the terrain lies on: `*surface`, or a plane without one.
    const Surface& SurfaceOrPlane() const {
        return terraingen::SurfaceOrPlane(surface);
    }

    /// Where noise on a plane is measured from: the source's `ReferencePoint`, or the origin
    /// without a source.
    Vec2d ReferencePoint() const {
        return terraingen::ReferencePoint(source);
    }
};

/// Checks that a layer that reads the ground beneath it was given one.
/// @param what What reads it, which starts the error message ("<what> need the ground").
/// @throws std::invalid_argument if `ground` is null.
void RequireGround(const Ground* ground, std::string_view what);

class Layer;

/// A shared, immutable layer, as a `Terrain` holds them.
using LayerPtr = std::shared_ptr<const Layer>;

/// The layers of a terrain that an update replaced, each with its replacement, in the terrain's
/// order; layers that read other layers rebind to them (`Layer::Rebound`).
using LayerReplacements = std::vector<std::pair<const Layer*, LayerPtr>>;

/// Relief added on top of a source: noise, craters, or a layer of your own.
/// Implementations of `ApplySamples` must be deterministic and thread-safe, and give a point the
/// height of a grid cell centered there.
class Layer {
  public:
    virtual ~Layer() = default;

    /// Adds the layer's relief to the cells of `heights` that hold data.
    void Apply(Raster<float>& heights, const LayerContext& context) const;

    /// Adds the layer's relief to every sample of `samples` that holds data.
    /// @throws std::invalid_argument for points without a positive `context.spacing`.
    void Apply(SampleBlock& samples, const LayerContext& context) const;

    /// The height at one point after this layer, from `height` before it.
    /// @throws std::invalid_argument without a positive `context.spacing`.
    float ApplyAt(float height, double x, double y, const LayerContext& context) const;

    /// The base a layer that derives state from it when built (craters, boulders) was built on,
    /// whose surface and source must be its terrain's; unset for a layer that reads them from
    /// its context.
    virtual std::optional<TerrainBase> LayerBase() const {
        return std::nullopt;
    }

    /// The other layers this one reads, which must come before it in its terrain's layers.
    virtual std::vector<const Layer*> Dependencies() const {
        return {};
    }

    /// A copy of the layer whose seeds draw from `seed` (`ReseedWord`); null for a layer without
    /// seeds (the default), which `Terrain::Reseed` keeps.
    virtual LayerPtr Reseeded(std::uint64_t /*seed*/) const {
        return nullptr;
    }

    /// A copy of the layer reading the layers `replaced` gives in place of those it reads
    /// (`Replacement`); null when it reads none of them, as for a layer without
    /// `Dependencies` (the default), which `Terrain::SetLayer` and `Terrain::Reseed` keep.
    virtual LayerPtr Rebound(const LayerReplacements& /*replaced*/) const {
        return nullptr;
    }

    /// A copy of a layer built on a base (`LayerBase`) rebuilt from its models and options on
    /// `base`; null for a layer without one (the default), which `Terrain::SetSource` and
    /// `Terrain::Reseed` keep.
    virtual LayerPtr Rebased(const TerrainBase& /*base*/) const {
        return nullptr;
    }

  protected:
    /// Adds the layer's relief to `samples`. A grid's spacing is `context.spacing` raised to the
    /// grid's own; points come with a positive one.
    virtual void ApplySamples(SampleBlock& samples, const LayerContext& context) const = 0;
};

/// The layer `replaced` gives in place of `layer`, or `layer` itself where it gives none or one
/// of another type.
template <typename T>
std::shared_ptr<const T> Replacement(const std::shared_ptr<const T>& layer,
                                     const LayerReplacements& replaced) {
    for (const auto& entry : replaced) {
        if (entry.first == layer.get()) {
            if (auto same = std::dynamic_pointer_cast<const T>(entry.second)) {
                return same;
            }
        }
    }
    return layer;
}

/// A layer whose height at a point depends only on the height there and the point (noise,
/// roughness, transforms); the base applies `HeightAt` to every sample in parallel.
class FormulaLayer : public Layer {
  protected:
    /// The height at (x, y) after the layer, from `height` before it.
    /// @param context The layer's context with `spacing` resolved to the sample spacing in meters
    /// and `ground` the one to read at this point, null when the context gives none.
    virtual float HeightAt(float height, double x, double y, const LayerContext& context) const = 0;

    /// Checks the context before any sample is evaluated; accepts any by default.
    virtual void CheckContext(const LayerContext& /*context*/) const {}

    void ApplySamples(SampleBlock& samples, const LayerContext& context) const final;
};

}  // namespace terraingen
