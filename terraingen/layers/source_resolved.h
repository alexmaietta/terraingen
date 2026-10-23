/// @file
/// @brief The feature size from which a source's measured data already holds a layer's features.
#pragma once

#include "terraingen/sources/field.h"
#include "terraingen/sources/source.h"
#include "terraingen/util/surface.h"

#include <memory>
#include <optional>

namespace terraingen {

/// The size in meters from which `source` resolves features at each point: `pixelsAcross` times
/// `Source::ResolutionAt` there, converted from the source's coordinates to meters with
/// `Surface::UnitLength`; infinity where the source resolves nothing, as over synthetic noise
/// without posts, outside a crop or in no-data holes. Its `Max` is infinity, a bound valid for any
/// source.
/// @param pixelsAcross The source cells across from which its data holds a feature; positive and
/// finite.
/// @param surface What the source's coordinates lie on: on a body they are degrees.
/// @throws std::invalid_argument for a null source or a `pixelsAcross` that is not positive and
/// finite.
std::shared_ptr<const SurfaceField> SourceResolvedLimit(std::shared_ptr<const Source> source,
                                                        double pixelsAcross,
                                                        const Surface& surface = {});

/// `SourceResolvedLimit` of a layer's `sourceResolvesPixels` over `source`, or null when either is
/// unset.
/// @throws std::invalid_argument as `SourceResolvedLimit`.
std::shared_ptr<const SurfaceField> LayerResolvedLimit(const std::shared_ptr<const Source>& source,
                                                       const std::optional<double>& pixelsAcross,
                                                       const Surface& surface);

/// Checks a layer's `sourceResolvesPixels`.
/// @throws std::invalid_argument "Source resolves pixels must be positive and finite" for a set
/// value that is not.
void ValidateSourceResolvesPixels(const std::optional<double>& pixelsAcross);

/// Whether a feature `size` meters across at surface (`x`, `y`) is already in the source: at least
/// `limit` there. False without a limit, or where it is NaN or infinite.
inline bool InSource(const SurfaceField* limit, double size, double x, double y) {
    return limit != nullptr && size >= limit->ValueAt(x, y);
}

}  // namespace terraingen
