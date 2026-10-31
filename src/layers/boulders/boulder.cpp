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
    m_x.reserve(count);
    m_y.reserve(count);
    m_diameter.reserve(count);
    m_height.reserve(count);
    m_burial.reserve(count);
    m_yaw.reserve(count);
    m_lean.reserve(count);
    m_id.reserve(count);
    m_rest.reserve(count);
    m_tilt.reserve(count);
    m_tiltAzimuth.reserve(count);
    m_ground.reserve(count);
    m_model.reserve(count);
    m_wear.reserve(count);
    m_parent.reserve(count);
    m_piece.reserve(count);
    m_facets.reserve(count);
    m_facetCount.reserve(count);
    m_shapeSeed.reserve(count);
    m_hasShapeSeed.reserve(count);
}

void BoulderList::Append(const Boulder& boulder) {
    m_x.push_back(boulder.x);
    m_y.push_back(boulder.y);
    m_diameter.push_back(boulder.diameter);
    m_height.push_back(boulder.height);
    m_burial.push_back(boulder.burial);
    m_yaw.push_back(boulder.yaw);
    m_lean.push_back(boulder.lean);
    m_id.push_back(boulder.id);
    m_rest.push_back(static_cast<std::uint8_t>(boulder.rest));
    m_tilt.push_back(boulder.tilt);
    m_tiltAzimuth.push_back(boulder.tiltAzimuth);
    m_ground.push_back(boulder.ground.value_or(std::numeric_limits<double>::quiet_NaN()));
    m_model.push_back(boulder.model ? 1 : 0);
    m_wear.push_back(boulder.wear);
    m_parent.push_back(boulder.parent);
    m_piece.push_back(boulder.piece);
    m_facets.push_back(boulder.facets);
    m_facetCount.push_back(boulder.facetCount);
    m_shapeSeed.push_back(boulder.shapeSeed.value_or(0));
    m_hasShapeSeed.push_back(boulder.shapeSeed ? 1 : 0);
}

void BoulderList::Append(const BoulderList& other) {
    AppendColumn(m_x, other.m_x);
    AppendColumn(m_y, other.m_y);
    AppendColumn(m_diameter, other.m_diameter);
    AppendColumn(m_height, other.m_height);
    AppendColumn(m_burial, other.m_burial);
    AppendColumn(m_yaw, other.m_yaw);
    AppendColumn(m_lean, other.m_lean);
    AppendColumn(m_id, other.m_id);
    AppendColumn(m_rest, other.m_rest);
    AppendColumn(m_tilt, other.m_tilt);
    AppendColumn(m_tiltAzimuth, other.m_tiltAzimuth);
    AppendColumn(m_ground, other.m_ground);
    AppendColumn(m_model, other.m_model);
    AppendColumn(m_wear, other.m_wear);
    AppendColumn(m_parent, other.m_parent);
    AppendColumn(m_piece, other.m_piece);
    AppendColumn(m_facets, other.m_facets);
    AppendColumn(m_facetCount, other.m_facetCount);
    AppendColumn(m_shapeSeed, other.m_shapeSeed);
    AppendColumn(m_hasShapeSeed, other.m_hasShapeSeed);
}

Boulder BoulderList::At(std::size_t index) const {
    if (index >= Size()) {
        throw std::out_of_range("Boulder index " + std::to_string(index) + " is out of range for " +
                                std::to_string(Size()) + " boulders");
    }
    Boulder boulder;
    boulder.x = m_x[index];
    boulder.y = m_y[index];
    boulder.diameter = m_diameter[index];
    boulder.height = m_height[index];
    boulder.burial = m_burial[index];
    boulder.yaw = m_yaw[index];
    boulder.lean = m_lean[index];
    boulder.id = m_id[index];
    boulder.rest = static_cast<BoulderRest>(m_rest[index]);
    boulder.tilt = m_tilt[index];
    boulder.tiltAzimuth = m_tiltAzimuth[index];
    if (!std::isnan(m_ground[index])) {
        boulder.ground = m_ground[index];
    }
    boulder.model = m_model[index] != 0;
    boulder.wear = m_wear[index];
    boulder.parent = m_parent[index];
    boulder.piece = m_piece[index];
    boulder.facets = m_facets[index];
    boulder.facetCount = m_facetCount[index];
    if (m_hasShapeSeed[index] != 0) {
        boulder.shapeSeed = m_shapeSeed[index];
    }
    return boulder;
}

bool BoulderList::operator==(const BoulderList& other) const {
    return SameDoubles(m_x, other.m_x) && SameDoubles(m_y, other.m_y) &&
           SameDoubles(m_diameter, other.m_diameter) && SameDoubles(m_height, other.m_height) &&
           SameDoubles(m_burial, other.m_burial) && SameDoubles(m_yaw, other.m_yaw) &&
           SameDoubles(m_lean, other.m_lean) && m_id == other.m_id && m_rest == other.m_rest &&
           SameDoubles(m_tilt, other.m_tilt) && SameDoubles(m_tiltAzimuth, other.m_tiltAzimuth) &&
           SameDoubles(m_ground, other.m_ground) && m_model == other.m_model &&
           SameDoubles(m_wear, other.m_wear) && m_parent == other.m_parent &&
           m_piece == other.m_piece && m_facets == other.m_facets &&
           m_facetCount == other.m_facetCount && m_shapeSeed == other.m_shapeSeed &&
           m_hasShapeSeed == other.m_hasShapeSeed;
}

}  // namespace terraingen
