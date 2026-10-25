// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief Craters, boulders and other values as NumPy columns.
#pragma once

#include "arrays.h"
#include "common.h"

#include "terraingen/layers/boulders/boulder.h"
#include "terraingen/layers/craters/generation/population.h"
#include "terraingen/layers/craters/shaping/attributes.h"
#include "terraingen/util/geometry.h"

#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

namespace terraingen::python {

/// The craters as NumPy columns, with their attributes when there is one per crater.
inline nb::dict CraterTable(const CraterList& craters,
                            const std::vector<CraterAttributes>& attributes = {}) {
    nb::dict table;
    table["x"] = Column(craters.X());
    table["y"] = Column(craters.Y());
    table["diameter"] = Column(craters.Diameters());
    table["age_gyr"] = Column(craters.AgesGyr());
    table["id"] = Column(craters.Ids());
    table["manual"] = Column(std::vector<std::uint8_t>(craters.Manual()));
    table["replace"] = Column(std::vector<std::uint8_t>(craters.Replace()));
    if (!attributes.empty() && attributes.size() == craters.Size()) {
        std::vector<double> floorDepth, rimHeight, depthRatio, freshness;
        std::vector<std::uint8_t> complex;
        for (const CraterAttributes& a : attributes) {
            floorDepth.push_back(a.floorDepth);
            rimHeight.push_back(a.rimHeight);
            depthRatio.push_back(a.depthRatio);
            freshness.push_back(a.freshness);
            complex.push_back(a.complex ? 1 : 0);
        }
        table["floor_depth"] = Column(std::move(floorDepth));
        table["rim_height"] = Column(std::move(rimHeight));
        table["depth_ratio"] = Column(std::move(depthRatio));
        table["freshness"] = Column(std::move(freshness));
        table["complex"] = Column(std::move(complex));
    }
    return table;
}

/// Radians as degrees.
inline std::vector<double> Degrees(const std::vector<double>& radians) {
    std::vector<double> degrees(radians.size());
    for (std::size_t i = 0; i < radians.size(); ++i) {
        degrees[i] = radians[i] * kRadian;
    }
    return degrees;
}

/// The boulders as NumPy columns, angles in degrees.
inline nb::dict BoulderTable(const BoulderList& boulders) {
    nb::dict table;
    table["x"] = Column(boulders.X());
    table["y"] = Column(boulders.Y());
    table["ground"] = Column(boulders.Grounds());
    table["diameter"] = Column(boulders.Diameters());
    table["height"] = Column(boulders.Heights());
    table["burial"] = Column(boulders.Burials());
    table["yaw_deg"] = Column(Degrees(boulders.Yaws()));
    table["lean_deg"] = Column(Degrees(boulders.Leans()));
    table["id"] = Column(boulders.Ids());
    table["model"] = Column(boulders.Models());
    table["wear"] = Column(boulders.Wears());
    table["parent"] = Column(boulders.Parents());
    table["piece"] = Column(boulders.Pieces());
    table["rest"] = Column(boulders.Rests());
    table["tilt_deg"] = Column(Degrees(boulders.Tilts()));
    table["tilt_azimuth_deg"] = Column(Degrees(boulders.TiltAzimuths()));
    return table;
}

}  // namespace terraingen::python
