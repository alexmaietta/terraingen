/// @file
/// @brief Preparing craters for one sample spacing.
#include "terraingen/layers/craters/shaping/prepared.h"

namespace terraingen {

std::vector<PreparedCrater> PrepareCraters(const CraterList& craters,
                                           const CraterApplyOptions& options, double spacing,
                                           const DegradationModel& degradation) {
    std::vector<PreparedCrater> prepared;
    for (const std::size_t index : CraterApplyOrder(craters, options.order)) {
        const Crater crater = craters.At(index);
        const double resolution = ResolutionWeight(crater.diameter, spacing, options.fade);
        if (resolution == 0.0) {
            continue;
        }
        prepared.push_back({crater, resolution, degradation.State(crater),
                            OutlineOf(crater, options.irregularity)});
    }
    return prepared;
}

}  // namespace terraingen
