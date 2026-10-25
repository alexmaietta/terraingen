// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/layers/craters/crater.h"

#include "terraingen/random/philox.h"
#include "terraingen/random/stream.h"
#include "terraingen/util/bits.h"
#include "terraingen/util/checks.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>

namespace terraingen {

std::uint64_t ManualCraterId(double x, double y, double diameter) {
    const IdWords xBits = SplitId(BitCast<std::uint64_t>(x));
    const IdWords yBits = SplitId(BitCast<std::uint64_t>(y));
    const IdWords dBits = SplitId(BitCast<std::uint64_t>(diameter));
    const U32x4 block =
        Philox4x32({xBits.low, xBits.high, yBits.low, yBits.high}, {dBits.low, dBits.high});
    return JoinWords(block.x, block.y);
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
        if (crater.depth && !IsPositive(*crater.depth)) {
            throw std::invalid_argument("A manual crater's depth must be unset or positive");
        }
        if (crater.rimHeight && !IsNonNegative(*crater.rimHeight)) {
            throw std::invalid_argument(
                "A manual crater's rim height must be unset or not negative");
        }
        if (!crater.depth && crater.rimHeight) {
            throw std::invalid_argument("A manual crater's rim height needs its depth too");
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

CraterList::CraterList(std::initializer_list<Crater> craters) {
    Reserve(craters.size());
    for (const Crater& crater : craters) {
        Append(crater);
    }
}

void CraterList::Reserve(std::size_t count) {
    x_.reserve(count);
    y_.reserve(count);
    diameter_.reserve(count);
    ageGyr_.reserve(count);
    id_.reserve(count);
    manual_.reserve(count);
    depth_.reserve(count);
    rimHeight_.reserve(count);
    replace_.reserve(count);
}

void CraterList::Append(const Crater& crater) {
    x_.push_back(crater.x);
    y_.push_back(crater.y);
    diameter_.push_back(crater.diameter);
    ageGyr_.push_back(crater.ageGyr);
    id_.push_back(crater.id);
    manual_.push_back(crater.manual ? 1 : 0);
    depth_.push_back(crater.depth.value_or(std::numeric_limits<double>::quiet_NaN()));
    rimHeight_.push_back(crater.rimHeight.value_or(std::numeric_limits<double>::quiet_NaN()));
    replace_.push_back(crater.replace ? 1 : 0);
}

void CraterList::Append(const CraterList& other) {
    x_.insert(x_.end(), other.x_.begin(), other.x_.end());
    y_.insert(y_.end(), other.y_.begin(), other.y_.end());
    diameter_.insert(diameter_.end(), other.diameter_.begin(), other.diameter_.end());
    ageGyr_.insert(ageGyr_.end(), other.ageGyr_.begin(), other.ageGyr_.end());
    id_.insert(id_.end(), other.id_.begin(), other.id_.end());
    manual_.insert(manual_.end(), other.manual_.begin(), other.manual_.end());
    depth_.insert(depth_.end(), other.depth_.begin(), other.depth_.end());
    rimHeight_.insert(rimHeight_.end(), other.rimHeight_.begin(), other.rimHeight_.end());
    replace_.insert(replace_.end(), other.replace_.begin(), other.replace_.end());
}

Crater CraterList::At(std::size_t index) const {
    if (index >= Size()) {
        throw std::out_of_range("Crater index out of range");
    }
    return {x_[index],
            y_[index],
            diameter_[index],
            ageGyr_[index],
            id_[index],
            manual_[index] != 0,
            UnlessNan(depth_[index]),
            UnlessNan(rimHeight_[index]),
            replace_[index] != 0};
}

bool CraterList::AnyReplace() const {
    return std::find(replace_.begin(), replace_.end(), std::uint8_t{1}) != replace_.end();
}

bool CraterSelection::Includes(const Crater& crater) const {
    return !((crater.manual && !crater.replace && !manual) || crater.diameter < minDiameter ||
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
