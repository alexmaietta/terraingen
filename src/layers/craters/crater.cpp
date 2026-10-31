// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/layers/craters/crater.h"

#include "terraingen/random/philox.h"
#include "terraingen/random/stream.h"
#include "terraingen/util/bits.h"
#include "util/checks.h"

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
    m_x.reserve(count);
    m_y.reserve(count);
    m_diameter.reserve(count);
    m_ageGyr.reserve(count);
    m_id.reserve(count);
    m_manual.reserve(count);
    m_depth.reserve(count);
    m_rimHeight.reserve(count);
    m_replace.reserve(count);
}

void CraterList::Append(const Crater& crater) {
    m_x.push_back(crater.x);
    m_y.push_back(crater.y);
    m_diameter.push_back(crater.diameter);
    m_ageGyr.push_back(crater.ageGyr);
    m_id.push_back(crater.id);
    m_manual.push_back(crater.manual ? 1 : 0);
    m_depth.push_back(crater.depth.value_or(std::numeric_limits<double>::quiet_NaN()));
    m_rimHeight.push_back(crater.rimHeight.value_or(std::numeric_limits<double>::quiet_NaN()));
    m_replace.push_back(crater.replace ? 1 : 0);
}

void CraterList::Append(const CraterList& other) {
    m_x.insert(m_x.end(), other.m_x.begin(), other.m_x.end());
    m_y.insert(m_y.end(), other.m_y.begin(), other.m_y.end());
    m_diameter.insert(m_diameter.end(), other.m_diameter.begin(), other.m_diameter.end());
    m_ageGyr.insert(m_ageGyr.end(), other.m_ageGyr.begin(), other.m_ageGyr.end());
    m_id.insert(m_id.end(), other.m_id.begin(), other.m_id.end());
    m_manual.insert(m_manual.end(), other.m_manual.begin(), other.m_manual.end());
    m_depth.insert(m_depth.end(), other.m_depth.begin(), other.m_depth.end());
    m_rimHeight.insert(m_rimHeight.end(), other.m_rimHeight.begin(), other.m_rimHeight.end());
    m_replace.insert(m_replace.end(), other.m_replace.begin(), other.m_replace.end());
}

Crater CraterList::At(std::size_t index) const {
    if (index >= Size()) {
        throw std::out_of_range("Crater index out of range");
    }
    return {m_x[index],
            m_y[index],
            m_diameter[index],
            m_ageGyr[index],
            m_id[index],
            m_manual[index] != 0,
            UnlessNan(m_depth[index]),
            UnlessNan(m_rimHeight[index]),
            m_replace[index] != 0};
}

bool CraterList::AnyReplace() const {
    return std::find(m_replace.begin(), m_replace.end(), std::uint8_t{1}) != m_replace.end();
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
