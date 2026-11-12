// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief Prepared craters turned into the records the crater kernel shapes.
#pragma once

#include "terraingen/geometry/surface.h"
#include "terraingen/layers/craters/profiles/profile.h"
#include "terraingen/layers/craters/shaping/apply_options.h"
#include "terraingen/layers/craters/shaping/prepared.h"
#include "terraingen/layers/kernels/craters.h"

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

/// The frame in which kernels shape points of body `surface` around `crater` (`BodySplitOffset`),
/// its center split (`SplitCoordinate`).
CraterFrame MakeCraterFrame(const Surface& surface, const Crater& crater);

/// `value` as a float, and the float of what that leaves (`SplitDifference`).
inline void SplitCoordinate(double value, float& high, float& low) {
    high = static_cast<float>(value);
    low = static_cast<float>(value - static_cast<double>(high));
}

/// Surface point `p` as kernels read it: its east and north on a plane, or its position
/// (`Surface::Position`) on a body, each split (`SplitCoordinate`).
SplitPoint MakeSplitPoint(const Surface& surface, const Vec2d& p);

/// `lists` as the kernels read them: each list's first entry and entry count appended to
/// `heads`, and the entries returned, never empty so a GPU can bind them.
std::vector<std::uint32_t> FlattenKernelLists(const std::vector<std::vector<std::uint32_t>>& lists,
                                              std::vector<std::uint32_t>& heads);

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

/// Sets the wall terraces of prepared crater `p` on its kernel record, their shape appended to
/// `data` (`KernelTerracedRadii`): none below the onset diameter, without `terraces.enabled` or
/// once worn below `terraces.minFreshness`. The floor edge is `profile`'s complex floor diameter,
/// else Pike's (1980, USGS PP 1046-C, eq. 15) 0.187 D^1.249 km; the bench's share of the
/// wall's slope sets benches `terraces.benchSlopeDegrees` steep on its mean slope.
void AppendKernelTerraces(const PreparedCrater& p, const CraterTerraces& terraces,
                          const CraterProfile& profile, CraterRecord& record,
                          std::vector<float>& data);

/// Sets the central peak massifs of prepared crater `p` on its kernel record, appended to `data`
/// (`KernelPeakMassifs`): none without `peaks.enabled`, for a `profile` that is not a
/// `ComplexCraterProfile` or below its transition diameter. Their roughness fades with the
/// crater's freshness, current over fresh depth.
void AppendKernelPeaks(const PreparedCrater& p, const CraterPeaks& peaks,
                       const CraterProfile& profile, CraterRecord& record,
                       std::vector<float>& data);

/// The records of `craters` of `prepared`, in order, with their datums as fitted; `profile`
/// tabulated for each when `kernel` asks for it. The data is never empty, so a GPU can bind it.
/// @throws std::invalid_argument for a crater state the profile rejects.
CraterKernelBatch MakeCraterKernelBatch(const CraterShapeKernel& kernel,
                                        const CraterProfile& profile,
                                        const std::vector<PreparedCrater>& prepared,
                                        const std::vector<std::size_t>& craters,
                                        const CraterApplyOptions& options);

}  // namespace terraingen
