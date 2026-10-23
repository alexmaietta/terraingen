// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/layers/boulders/boulder.h"

#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>

namespace terraingen {

namespace {

template <typename T> void AppendColumn(std::vector<T>& column, const std::vector<T>& other) {
    column.insert(column.end(), other.begin(), other.end());
}

/// Whether two columns of doubles hold the same values, NaN equal to NaN.
bool SameDoubles(const std::vector<double>& a, const std::vector<double>& b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (!(a[i] == b[i] || (std::isnan(a[i]) && std::isnan(b[i])))) {
            return false;
        }
    }
    return true;
}

}  // namespace

BoulderList::BoulderList(std::initializer_list<Boulder> boulders) {
    Reserve(boulders.size());
    for (const Boulder& boulder : boulders) {
        Append(boulder);
    }
}

void BoulderList::Reserve(std::size_t count) {
    x_.reserve(count);
    y_.reserve(count);
    diameter_.reserve(count);
    height_.reserve(count);
    burial_.reserve(count);
    yaw_.reserve(count);
    lean_.reserve(count);
    id_.reserve(count);
    rest_.reserve(count);
    tilt_.reserve(count);
    tiltAzimuth_.reserve(count);
    ground_.reserve(count);
    model_.reserve(count);
    wear_.reserve(count);
    parent_.reserve(count);
    piece_.reserve(count);
    facets_.reserve(count);
    facetCount_.reserve(count);
    shapeSeed_.reserve(count);
    hasShapeSeed_.reserve(count);
}

void BoulderList::Append(const Boulder& boulder) {
    x_.push_back(boulder.x);
    y_.push_back(boulder.y);
    diameter_.push_back(boulder.diameter);
    height_.push_back(boulder.height);
    burial_.push_back(boulder.burial);
    yaw_.push_back(boulder.yaw);
    lean_.push_back(boulder.lean);
    id_.push_back(boulder.id);
    rest_.push_back(static_cast<std::uint8_t>(boulder.rest));
    tilt_.push_back(boulder.tilt);
    tiltAzimuth_.push_back(boulder.tiltAzimuth);
    ground_.push_back(boulder.ground.value_or(std::numeric_limits<double>::quiet_NaN()));
    model_.push_back(boulder.model ? 1 : 0);
    wear_.push_back(boulder.wear);
    parent_.push_back(boulder.parent);
    piece_.push_back(boulder.piece);
    facets_.push_back(boulder.facets);
    facetCount_.push_back(boulder.facetCount);
    shapeSeed_.push_back(boulder.shapeSeed.value_or(0));
    hasShapeSeed_.push_back(boulder.shapeSeed ? 1 : 0);
}

void BoulderList::Append(const BoulderList& other) {
    AppendColumn(x_, other.x_);
    AppendColumn(y_, other.y_);
    AppendColumn(diameter_, other.diameter_);
    AppendColumn(height_, other.height_);
    AppendColumn(burial_, other.burial_);
    AppendColumn(yaw_, other.yaw_);
    AppendColumn(lean_, other.lean_);
    AppendColumn(id_, other.id_);
    AppendColumn(rest_, other.rest_);
    AppendColumn(tilt_, other.tilt_);
    AppendColumn(tiltAzimuth_, other.tiltAzimuth_);
    AppendColumn(ground_, other.ground_);
    AppendColumn(model_, other.model_);
    AppendColumn(wear_, other.wear_);
    AppendColumn(parent_, other.parent_);
    AppendColumn(piece_, other.piece_);
    AppendColumn(facets_, other.facets_);
    AppendColumn(facetCount_, other.facetCount_);
    AppendColumn(shapeSeed_, other.shapeSeed_);
    AppendColumn(hasShapeSeed_, other.hasShapeSeed_);
}

Boulder BoulderList::At(std::size_t index) const {
    if (index >= Size()) {
        throw std::out_of_range("Boulder index " + std::to_string(index) + " is out of range for " +
                                std::to_string(Size()) + " boulders");
    }
    Boulder boulder;
    boulder.x = x_[index];
    boulder.y = y_[index];
    boulder.diameter = diameter_[index];
    boulder.height = height_[index];
    boulder.burial = burial_[index];
    boulder.yaw = yaw_[index];
    boulder.lean = lean_[index];
    boulder.id = id_[index];
    boulder.rest = static_cast<BoulderRest>(rest_[index]);
    boulder.tilt = tilt_[index];
    boulder.tiltAzimuth = tiltAzimuth_[index];
    if (!std::isnan(ground_[index])) {
        boulder.ground = ground_[index];
    }
    boulder.model = model_[index] != 0;
    boulder.wear = wear_[index];
    boulder.parent = parent_[index];
    boulder.piece = piece_[index];
    boulder.facets = facets_[index];
    boulder.facetCount = facetCount_[index];
    if (hasShapeSeed_[index] != 0) {
        boulder.shapeSeed = shapeSeed_[index];
    }
    return boulder;
}

bool BoulderList::operator==(const BoulderList& other) const {
    return SameDoubles(x_, other.x_) && SameDoubles(y_, other.y_) &&
           SameDoubles(diameter_, other.diameter_) && SameDoubles(height_, other.height_) &&
           SameDoubles(burial_, other.burial_) && SameDoubles(yaw_, other.yaw_) &&
           SameDoubles(lean_, other.lean_) && id_ == other.id_ && rest_ == other.rest_ &&
           SameDoubles(tilt_, other.tilt_) && SameDoubles(tiltAzimuth_, other.tiltAzimuth_) &&
           SameDoubles(ground_, other.ground_) && model_ == other.model_ &&
           SameDoubles(wear_, other.wear_) && parent_ == other.parent_ && piece_ == other.piece_ &&
           facets_ == other.facets_ && facetCount_ == other.facetCount_ &&
           shapeSeed_ == other.shapeSeed_ && hasShapeSeed_ == other.hasShapeSeed_;
}

}  // namespace terraingen
