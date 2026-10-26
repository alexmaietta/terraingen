// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/layers/craters/crater_placer.h"

#include <utility>

namespace terraingen {

void CraterFeatures::Append(const CraterFeatures& other) {
    craters.Append(other.craters);
    attributes.insert(attributes.end(), other.attributes.begin(), other.attributes.end());
}

CraterFeatures DescribeCraters(CraterList craters, const CraterShapeModels& models) {
    CraterFeatures described{std::move(craters), {}};
    described.attributes.reserve(described.craters.Size());
    for (std::size_t n = 0; n < described.craters.Size(); ++n) {
        described.attributes.push_back(models.Attributes(described.craters.At(n)));
    }
    return described;
}

}  // namespace terraingen
