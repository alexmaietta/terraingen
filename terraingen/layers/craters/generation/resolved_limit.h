/// @file
/// @brief The crater diameter above which a source's measured data already holds the craters.
#pragma once

#include "terraingen/sources/field.h"
#include "terraingen/sources/source.h"
#include "terraingen/util/surface.h"

#include <memory>

namespace terraingen {

/// The diameter in metres from which `source` resolves craters at each point: `pixelsAcross`
/// times `Source::ResolutionAt` there, converted from the source's coordinates to metres with
/// `Surface::UnitLength`; infinity where the source resolves nothing, as over synthetic or flat
/// sources.
///
/// A DEM already holds the craters at least `pixelsAcross` of its cells across, so set
/// `CraterGenerationOptions::completeness` to this field to generate only the smaller ones
/// (`MinimumField` with a catalog's own limit). The field's `Max` is infinity, a bound valid for
/// any source.
/// @param pixelsAcross The number of cells across from which the data resolves a crater, the
/// caller's choice; positive and finite.
/// @param surface What the source's coordinates lie on: on a body they are degrees.
/// @throws std::invalid_argument for a null source or a `pixelsAcross` that is not positive and
/// finite.
std::shared_ptr<const SurfaceField> UnresolvedCraterLimit(std::shared_ptr<const Source> source,
                                                          double pixelsAcross,
                                                          const Surface& surface = {});

}  // namespace terraingen
