// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief `ApplyCraterTo`, one crater applied at one point through the crater kernel.
#pragma once

#include "layers/craters/shaping/crater_kernel.h"
#include "terraingen/layers/craters/shaping/apply.h"

namespace terraingen::testing {

/// The height at surface (x, y) after one crater, bit-identical to what `ApplyCraters` does to a
/// cell centered there; `height` beyond its reach.
/// @param crater With its datum when the options have one.
inline float ApplyCraterTo(float height, double x, double y, const PreparedCrater& crater,
                           const CraterProfile& profile, const CraterApplyOptions& options) {
    const CraterShapeKernel kernel = MakeCraterShapeKernel(profile, options);
    const CraterKernelBatch batch = MakeCraterKernelBatch(kernel, profile, {crater}, {0}, options);
    const KernelOffset at = ShapingOffset(options.surface, crater.crater, {x, y});
    return ApplyCraterKernel(kernel, batch.Data(), batch.records[0], height, at.dx, at.dy);
}

}  // namespace terraingen::testing
