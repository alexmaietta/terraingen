#include "terraingen/layers/craters/crater.h"

#include "terraingen/random/philox.h"
#include "terraingen/random/stream.h"
#include "terraingen/util/checks.h"

#include <bit>
#include <cmath>
#include <stdexcept>
#include <string>

namespace terraingen {

std::uint64_t ManualCraterId(double x, double y, double diameter) {
    const IdWords xBits = SplitId(std::bit_cast<std::uint64_t>(x));
    const IdWords yBits = SplitId(std::bit_cast<std::uint64_t>(y));
    const IdWords dBits = SplitId(std::bit_cast<std::uint64_t>(diameter));
    const U32x4 block =
        Philox4x32({xBits.low, xBits.high, yBits.low, yBits.high}, {dBits.low, dBits.high});
    return (static_cast<std::uint64_t>(block.x) << 32) | block.y;
}

void AppendManualCraters(const CraterList& manual, CraterList& craters,
                         const std::function<bool(const Crater&)>& keep) {
    for (std::size_t n = 0; n < manual.Size(); ++n) {
        Crater crater = manual.At(n);
        if (!(std::isfinite(crater.x) && std::isfinite(crater.y) && IsPositive(crater.diameter) &&
              IsNonNegative(crater.ageGyr))) {
            throw std::invalid_argument("Manual craters need finite positions, positive diameters "
                                        "and non-negative ages");
        }
        crater.manual = true;
        if (crater.id == 0) {
            crater.id = ManualCraterId(crater.x, crater.y, crater.diameter);
        }
        if (!keep || keep(crater)) {
            craters.Append(crater);
        }
    }
}

void CraterList::Reserve(std::size_t count) {
    x_.reserve(count);
    y_.reserve(count);
    diameter_.reserve(count);
    ageGyr_.reserve(count);
    id_.reserve(count);
    manual_.reserve(count);
}

void CraterList::Append(const Crater& crater) {
    x_.push_back(crater.x);
    y_.push_back(crater.y);
    diameter_.push_back(crater.diameter);
    ageGyr_.push_back(crater.ageGyr);
    id_.push_back(crater.id);
    manual_.push_back(crater.manual ? 1 : 0);
}

void CraterList::Append(const CraterList& other) {
    x_.insert(x_.end(), other.x_.begin(), other.x_.end());
    y_.insert(y_.end(), other.y_.begin(), other.y_.end());
    diameter_.insert(diameter_.end(), other.diameter_.begin(), other.diameter_.end());
    ageGyr_.insert(ageGyr_.end(), other.ageGyr_.begin(), other.ageGyr_.end());
    id_.insert(id_.end(), other.id_.begin(), other.id_.end());
    manual_.insert(manual_.end(), other.manual_.begin(), other.manual_.end());
}

Crater CraterList::At(std::size_t index) const {
    if (index >= Size()) {
        throw std::out_of_range("Crater index out of range");
    }
    return {x_[index],      y_[index],  diameter_[index],
            ageGyr_[index], id_[index], manual_[index] != 0};
}

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
