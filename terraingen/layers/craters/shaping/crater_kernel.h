/// @file
/// @brief Prepared craters turned into the records the crater kernel shapes.
#pragma once

#include "terraingen/layers/craters/profiles/profile.h"
#include "terraingen/layers/craters/shaping/apply_options.h"
#include "terraingen/layers/craters/shaping/prepared.h"
#include "terraingen/layers/kernels/craters.h"

#include <memory>
#include <optional>
#include <vector>

namespace terraingen {

/// The kernel form of `profile` shaped as `options` say, or nothing when the kernel cannot shape
/// it: a profile of your own, or craters on a body.
std::optional<CraterShapeKernel> MakeCraterShapeKernel(const CraterProfile& profile,
                                                       const CraterApplyOptions& options);

/// Craters ready for the kernel: one record per crater, and the values that vary in length.
struct CraterKernelBatch {
    std::vector<CraterRecord> records;
    std::vector<float> data;

    /// Where the kernel reads `data`.
    CraterData Data() const {
        CraterData view;
        view.values = data.data();
        return view;
    }
};

/// The records of `craters` of `prepared`, in order, with their datums as fitted.
/// @throws std::invalid_argument for a crater state the profile rejects.
CraterKernelBatch MakeCraterKernelBatch(const CraterShapeKernel& kernel,
                                        const CraterProfile& profile,
                                        const std::vector<PreparedCrater>& prepared,
                                        const std::vector<std::size_t>& craters,
                                        const CraterApplyOptions& options);

}  // namespace terraingen
