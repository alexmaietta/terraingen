// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief The feature size from which a source's measured data already holds a layer's features.
#pragma once

#include "terraingen/geometry/surface.h"
#include "terraingen/layers/field.h"
#include "terraingen/sources/source.h"

#include <memory>
#include <optional>

namespace terraingen {

/// The size in meters from which `source` resolves features at each point, `pixelsAcross` times
/// its resolution there; infinity where it resolves nothing.
/// @param pixelsAcross Source cells across a resolved feature; positive and finite.
/// @throws std::invalid_argument for a null source or a bad `pixelsAcross`.
std::shared_ptr<const SurfaceField> SourceResolvedLimit(std::shared_ptr<const Source> source,
                                                        double pixelsAcross,
                                                        const Surface& surface = {});

/// `SourceResolvedLimit` of a layer's `sourceResolvesPixels` over `source`, or null when either is
/// unset.
/// @throws std::invalid_argument as `SourceResolvedLimit`.
std::shared_ptr<const SurfaceField> LayerResolvedLimit(const std::shared_ptr<const Source>& source,
                                                       const std::optional<double>& pixelsAcross,
                                                       const Surface& surface);

/// The value of `LayerResolvedLimit` at surface (`x`, `y`) without building the field: the size in
/// meters from which `source` resolves features there, infinity when either is unset.
double SourceResolvedSize(const Source* source, const std::optional<double>& pixelsAcross,
                          const Surface& surface, double x, double y);

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
