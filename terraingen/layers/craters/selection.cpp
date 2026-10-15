#include "terraingen/layers/craters/selection.h"

#include <stdexcept>
#include <string>

namespace terraingen {

bool CraterSelection::Includes(const Crater& crater) const {
    return !((crater.manual && !manual) || crater.diameter < minDiameter ||
             crater.diameter > maxDiameter || crater.ageGyr < minAgeGyr ||
             crater.ageGyr > maxAgeGyr);
}

void ValidateCraterSelection(const CraterSelection& selection, std::string_view what) {
    if (!(selection.minDiameter >= 0.0 && selection.maxDiameter >= selection.minDiameter &&
          selection.minAgeGyr >= 0.0 && selection.maxAgeGyr >= selection.minAgeGyr)) {
        throw std::invalid_argument(std::string(what) +
                                    " size and age ranges must satisfy 0 <= min <= max");
    }
}

}  // namespace terraingen
