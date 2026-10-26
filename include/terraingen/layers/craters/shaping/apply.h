// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief Applying a list of craters to heights, over a raster or at scattered points.
#pragma once

#include "terraingen/gpu/backend.h"
#include "terraingen/layers/craters/degradation/degradation_model.h"
#include "terraingen/layers/craters/generation/population.h"
#include "terraingen/layers/craters/profiles/profile.h"
#include "terraingen/layers/craters/shaping/apply_options.h"
#include "terraingen/layers/craters/shaping/attributes.h"
#include "terraingen/layers/craters/shaping/datum.h"
#include "terraingen/layers/craters/shaping/prepared.h"
#include "terraingen/layers/evaluation_cache.h"
#include "terraingen/layers/ground.h"
#include "terraingen/layers/layer.h"
#include "terraingen/layers/sample_block.h"
#include "terraingen/raster/raster.h"

#include <functional>
#include <memory>

namespace terraingen {

/// The models craters are shaped with: their profile and the degradation state it is drawn in.
struct CraterShapeModels {
    std::shared_ptr<const CraterProfile> profile;         ///< Height by distance from the center.
    std::shared_ptr<const DegradationModel> degradation;  ///< Each crater's state.

    /// A crater's depth, rim height and freshness in its `ResolvedCraterState`.
    /// @throws std::domain_error as `DegradationModel::State`.
    /// @throws std::invalid_argument as `ResolvedCraterState`.
    CraterAttributes Attributes(const Crater& crater) const;
};

/// How `ApplyCraters` samples a raster or points.
struct CraterSampling {
    /// Sample spacing in world units; 0 or anything finer means the raster's cell size.
    double spacing = 0.0;
    unsigned threads = 1;  ///< Worker threads, 0 for all; the result does not depend on it.
    /// The ground datums are fitted to; empty for the raster as passed in, interpolated
    /// bilinearly.
    HeightSampler ground{};
    /// `ground` at many points in one pass, to the same values; empty reads them one at a time.
    HeightsSampler groundBatch{};
    /// Changes each crater's outline after its irregularity, such as secondaries stretched toward
    /// their primary; empty leaves outlines as they are. Must be deterministic and thread-safe.
    std::function<void(const Crater&, CraterOutline&)> reshape{};
    /// Where point queries keep the datums they fit, shared across batches; null to fit each time.
    EvaluationCache* cache = nullptr;
    const void* owner = nullptr;  ///< The layer the cached datums belong to.
    /// Where grids are shaped: the CPU, or a GPU backend when it can take them, to within float
    /// rounding of the CPU; points always run on the CPU.
    Backend backend = Backend::Cpu;
};

/// The sampling a crater layer evaluated in `context` applies its craters with: `spacing`, and the
/// context's threads, ground, cache and backend, with cached datums kept under `owner`.
CraterSampling SamplingFrom(const LayerContext& context, double spacing, const void* owner);

/// How far, in crater radii, a crater changes the terrain: the profile's reach, or the ejecta's
/// when `options.texture` draws beyond the rim and they reach further.
double CraterReachRadii(const CraterProfile& profile, const CraterApplyOptions& options);

/// Applies craters to a raster in the order `options` gives, each weighted by `ResolutionWeight`;
/// no-data cells are left unchanged.
/// @param craters With a datum, every crater within `DatumMargin` (or `ReplacementMargin`).
/// @param models Neither may be null.
/// @throws std::invalid_argument for invalid `options` or a negative or non-finite spacing.
/// @throws std::domain_error if the raster's transform is not invertible.
void ApplyCraters(Raster<float>& raster, const CraterList& craters, const CraterShapeModels& models,
                  const CraterApplyOptions& options = {}, const CraterSampling& sampling = {});

/// `ApplyCraters` on a sample block: a grid as above, or scattered points, each of which ends at
/// the same height as a grid cell centered there.
/// @throws std::invalid_argument as above; for points also without a positive spacing, or with a
/// datum or a replacing crater reaching them and no `sampling.ground`.
void ApplyCraters(SampleBlock& samples, const CraterList& craters, const CraterShapeModels& models,
                  const CraterApplyOptions& options = {}, const CraterSampling& sampling = {});

/// `ApplyCraters` at points with a datum, taking every datum from `sampling.cache` so `craters`
/// need only reach the points, not the craters beneath their datums; false, leaving the samples as
/// they are, for a grid, without a datum, cache or ground, or when a datum is not held yet.
/// @throws std::invalid_argument as `ApplyCraters`.
bool ApplyCratersOnCachedDatums(SampleBlock& samples, const CraterList& craters,
                                const CraterShapeModels& models, const CraterApplyOptions& options,
                                const CraterSampling& sampling);

}  // namespace terraingen
