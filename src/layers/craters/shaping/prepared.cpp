// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/layers/craters/shaping/prepared.h"

#include "layers/craters/shaping/resolved_state.h"
#include "terraingen/layers/craters/shaping/apply.h"
#include "terraingen/util/bits.h"
#include "terraingen/util/parallel.h"

#include <algorithm>
#include <array>
#include <optional>
#include <utility>

namespace terraingen {

namespace {

/// Craters each thread keeps prepared for reuse.
constexpr std::size_t kRecentCraters = 512;

/// A crater a thread prepared: whose it is, the crater, and its outline and state.
struct RecentCrater {
    PreparedReuse reuse;
    Crater crater;
    CraterOutline outline;
    CraterState state;
};

/// The outline and state of `crater`, reshaped by `reshape`, reused from this thread's last
/// preparation of it for `reuse` when there is one.
std::pair<CraterOutline, CraterState> OutlineAndState(
    const Crater& crater, const CraterApplyOptions& options, const CraterShapeModels& models,
    const std::function<void(const Crater&, CraterOutline&)>& reshape, PreparedReuse reuse) {
    const auto prepare = [&] {
        CraterOutline outline = IrregularOutline(crater, options.irregularity);
        if (reshape) {
            reshape(crater, outline);
        }
        return std::pair{std::move(outline), ResolvedCraterState(models, crater)};
    };
    if (reuse.cache == 0) {
        return prepare();
    }
    thread_local std::array<std::optional<RecentCrater>, kRecentCraters> recent;
    std::optional<RecentCrater>& slot =
        recent[static_cast<std::size_t>((crater.id * 0x9E3779B97F4A7C15ULL) >> 55U)];
    if (slot && slot->reuse.cache == reuse.cache && slot->reuse.owner == reuse.owner &&
        slot->crater == crater) {
        return {slot->outline, slot->state};
    }
    std::pair<CraterOutline, CraterState> prepared = prepare();
    slot = RecentCrater{reuse, crater, prepared.first, prepared.second};
    return prepared;
}

}  // namespace

std::vector<PreparedCrater>
PrepareCraters(const CraterList& craters, const CraterApplyOptions& options, double spacing,
               const CraterShapeModels& models, unsigned threads,
               const std::function<void(const Crater&, CraterOutline&)>& reshape,
               PreparedReuse reuse) {
    constexpr std::size_t kChunk = 64;
    const std::vector<std::size_t> order = CraterApplyOrder(craters, options.order);
    std::vector<PreparedCrater> prepared(order.size());
    ParallelFor(CeilDiv(order.size(), kChunk), threads, [&](std::size_t chunk) {
        const std::size_t end = std::min(order.size(), (chunk + 1) * kChunk);
        for (std::size_t n = chunk * kChunk; n < end; ++n) {
            PreparedCrater& entry = prepared[n];
            entry.crater = craters.At(order[n]);
            entry.resolution = ResolutionWeight(entry.crater.diameter, spacing, options.fade);
            if (entry.resolution == 0.0) {
                continue;
            }
            auto [outline, state] = OutlineAndState(entry.crater, options, models, reshape, reuse);
            entry.outline = std::move(outline);
            entry.state = state;
        }
    });
    const auto kept =
        std::remove_if(prepared.begin(), prepared.end(),
                       [](const PreparedCrater& entry) { return entry.resolution == 0.0; });
    prepared.erase(kept, prepared.end());
    return prepared;
}

}  // namespace terraingen
