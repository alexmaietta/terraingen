// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief `PreparedCrater`, a crater with everything needed to shape it at one sample spacing.
#pragma once

#include "terraingen/layers/craters/crater.h"
#include "terraingen/layers/craters/degradation/degradation_model.h"
#include "terraingen/layers/craters/shaping/apply_options.h"
#include "terraingen/layers/craters/shaping/irregularity.h"
#include "terraingen/util/plane_fit.h"

#include <functional>
#include <optional>
#include <vector>

namespace terraingen {

/// A crater ready to shape at one sample spacing.
struct PreparedCrater {
    Crater crater;                 ///< The crater.
    double resolution = 1.0;       ///< `ResolutionWeight` at the sample spacing.
    CraterState state;             ///< Its degradation state.
    CraterOutline outline{};       ///< Its outline.
    std::optional<Plane> datum{};  ///< From `FitCraterDatum`; none without a datum.
    /// Its asymmetry on the ground beneath (`ShapeOnSlopes`); level without a slope response.
    CraterSlopeShape slope{};
};

/// How far (m) from its center `prepared` reaches at `reachRadii` radii, stretched by its outline.
inline double CraterReach(const PreparedCrater& prepared, double reachRadii) {
    return prepared.outline.MaxScale() * reachRadii * (0.5 * prepared.crater.diameter);
}

struct CraterShapeModels;

/// Whose outlines and states `PrepareCraters` may reuse from those a thread prepared before: an
/// `EvaluationCache::Id` and the layer that owns the craters; a zero `cache` reuses nothing. The
/// owner must prepare each crater with the same options, models and reshaping every time.
struct PreparedReuse {
    std::uint64_t cache = 0;
    const void* owner = nullptr;
};

/// The craters with a non-zero resolution weight at `spacing`, prepared in `CraterApplyOrder`
/// without datums, each in its `ResolvedCraterState`; `reshape`, when set, changes each outline
/// after `IrregularOutline` and may be called on several threads at once.
/// @param spacing Sample spacing in meters; positive.
/// @param models Neither may be null.
/// @param threads Threads to prepare the craters over; 0 for every hardware thread.
/// @param reuse Whose craters a thread prepared before may be reused, as point queries do.
/// @throws std::domain_error as `DegradationModel::State`.
/// @throws std::invalid_argument as `ResolvedCraterState`.
std::vector<PreparedCrater>
PrepareCraters(const CraterList& craters, const CraterApplyOptions& options, double spacing,
               const CraterShapeModels& models, unsigned threads,
               const std::function<void(const Crater&, CraterOutline&)>& reshape = {},
               PreparedReuse reuse = {});

}  // namespace terraingen
