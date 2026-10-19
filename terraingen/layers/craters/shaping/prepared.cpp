#include "terraingen/layers/craters/shaping/prepared.h"

#include "terraingen/layers/craters/shaping/apply.h"
#include "terraingen/layers/craters/shaping/explicit_state.h"

#include <utility>

namespace terraingen {

std::vector<PreparedCrater>
PrepareCraters(const CraterList& craters, const CraterApplyOptions& options, double spacing,
               const CraterShapeModels& models,
               const std::function<void(const Crater&, CraterOutline&)>& reshape) {
    std::vector<PreparedCrater> prepared;
    for (const std::size_t index : CraterApplyOrder(craters, options.order)) {
        const Crater crater = craters.At(index);
        const double resolution = ResolutionWeight(crater.diameter, spacing, options.fade);
        if (resolution == 0.0) {
            continue;
        }
        CraterOutline outline = IrregularOutline(crater, options.irregularity);
        if (reshape) {
            reshape(crater, outline);
        }
        prepared.push_back(
            {crater, resolution, ResolvedCraterState(models, crater), std::move(outline)});
    }
    return prepared;
}

}  // namespace terraingen
