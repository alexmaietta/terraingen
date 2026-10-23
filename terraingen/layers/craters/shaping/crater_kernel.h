/// @file
/// @brief Prepared craters turned into the records the crater kernel shapes.
#pragma once

#include "terraingen/layers/craters/profiles/profile.h"
#include "terraingen/layers/craters/shaping/apply_options.h"
#include "terraingen/layers/craters/shaping/prepared.h"
#include "terraingen/layers/kernels/craters.h"
#include "terraingen/util/surface.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace terraingen {

/// Cells of the row each crater's profile is tabulated in when the kernel does not know the
/// profile (`CraterProfileKind::Tabulated`).
constexpr std::uint32_t kProfileTableCells = 512;

/// The kernel form of `profile` shaped as `options` say. Profiles the kernel knows (Fassett and
/// Thomson, Martin, Parkes and Dunstan, complex and diffused forms of them) are evaluated in
/// closed form; any other, such as a profile of your own, is tabulated per crater from its
/// `Height`.
CraterShapeKernel MakeCraterShapeKernel(const CraterProfile& profile,
                                        const CraterApplyOptions& options);

/// The offset at which the kernel shapes surface point `point` from `crater`'s center:
/// `Surface::Offset` in single precision, so exactly the difference on a plane and the crater's
/// local frame on a body.
inline KernelOffset ShapingOffset(const Surface& surface, const Crater& crater,
                                  const Vec2d& point) {
    const Vec2d offset = surface.Offset({crater.x, crater.y}, point);
    return {static_cast<float>(offset.x), static_cast<float>(offset.y)};
}

/// The frame in which the GPU shapes points of body `surface` around `crater`
/// (`KernelFrameOffset`), with positions measured from `origin` (m).
CraterFrame MakeCraterFrame(const Surface& surface, const Crater& crater, const Vec3d& origin);

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

/// The records of `craters` of `prepared`, in order, with their datums as fitted; `profile`
/// tabulated for each when `kernel` asks for it.
/// @throws std::invalid_argument for a crater state the profile rejects.
CraterKernelBatch MakeCraterKernelBatch(const CraterShapeKernel& kernel,
                                        const CraterProfile& profile,
                                        const std::vector<PreparedCrater>& prepared,
                                        const std::vector<std::size_t>& craters,
                                        const CraterApplyOptions& options);

}  // namespace terraingen
