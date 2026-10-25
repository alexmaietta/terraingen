// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief `ResolvedCraterState`, a crater's state with a manual crater's explicit depth and rim
/// height applied.
#pragma once

#include "terraingen/layers/craters/crater.h"
#include "terraingen/layers/craters/degradation/degradation_model.h"
#include "terraingen/layers/craters/degradation/state.h"
#include "terraingen/layers/craters/profiles/profile.h"

namespace terraingen {

struct CraterShapeModels;

/// The state a crater is shaped and described in.
///
/// Without an explicit depth it is `degradation.State`, or `WornState` for a `rate` above 1. With
/// one, `rate` is ignored and the current depth over diameter is `crater.depth` / D; the rim height
/// is `crater.rimHeight` / D, or without it the fresh rim lowered in proportion to the depth (as
/// `FreshnessDegradation` wears it). The fresh morphometry is raised to at least the current one.
/// Under a `DiffusedProfile`, which shapes from the fresh state and diffusion age alone, the
/// diffusion age is solved for the depth; a depth at least the fresh one is taken as the fresh
/// depth with no diffusion.
/// @param rate How many times as fast as on level ground the crater wore; at least 1.
/// @throws std::invalid_argument for an explicit depth that is not positive and finite, a rim
/// height without a depth, an explicit rim height under a `DiffusedProfile` or for a
/// `FassettThomsonProfile` simple crater, a depth deeper than the Fassett-Thomson fit reaches, or
/// one shallower than diffusion over the tabulated ages wears the crater.
/// @throws std::domain_error as `DegradationModel::State`.
CraterState ResolvedCraterState(const CraterProfile& profile, const DegradationModel& degradation,
                                const Crater& crater, double rate = 1.0);

/// Whether `profile` takes an explicit rim height for a crater `diameter` meters across: not a
/// `DiffusedProfile`, nor a `FassettThomsonProfile` simple crater, which tie it to the depth.
bool TakesRimHeight(const CraterProfile& profile, double diameter);

/// `ResolvedCraterState` with the models' profile and degradation, neither null.
CraterState ResolvedCraterState(const CraterShapeModels& models, const Crater& crater,
                                double rate = 1.0);

}  // namespace terraingen
