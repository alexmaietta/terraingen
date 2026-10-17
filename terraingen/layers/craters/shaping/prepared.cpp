#include "terraingen/layers/craters/shaping/prepared.h"

#include <utility>

namespace terraingen {

std::vector<PreparedCrater>
PrepareCraters(const CraterList& craters, const CraterApplyOptions& options, double spacing,
               const DegradationModel& degradation,
               const std::function<void(const Crater&, CraterOutline&)>& reshape) {
    std::vector<PreparedCrater> prepared;
    for (const std::size_t index : CraterApplyOrder(craters, options.order)) {
        const Crater crater = craters.At(index);
        const double resolution = ResolutionWeight(crater.diameter, spacing, options.fade);
        if (resolution == 0.0) {
            continue;
        }
        CraterOutline outline = OutlineOf(crater, options.irregularity);
        if (reshape) {
            reshape(crater, outline);
        }
        prepared.push_back({crater, resolution, degradation.State(crater), std::move(outline)});
    }
    return prepared;
}

}  // namespace terraingen
