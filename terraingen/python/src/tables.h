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

/// The boulders as NumPy columns, angles in degrees.
inline nb::dict BoulderTable(const std::vector<Boulder>& boulders) {
    std::vector<double> x, y, ground, diameter, height, burial, yaw, lean, wear, tilt, azimuth;
    std::vector<std::uint64_t> id, parent;
    std::vector<std::uint32_t> piece;
    std::vector<std::uint8_t> model, rest;
    for (const Boulder& boulder : boulders) {
        x.push_back(boulder.x);
        y.push_back(boulder.y);
        ground.push_back(boulder.ground.value_or(std::numeric_limits<double>::quiet_NaN()));
        diameter.push_back(boulder.diameter);
        height.push_back(boulder.height);
        burial.push_back(boulder.burial);
        yaw.push_back(boulder.yaw * kRadian);
        lean.push_back(boulder.lean * kRadian);
        id.push_back(boulder.id);
        model.push_back(boulder.model ? 1 : 0);
        wear.push_back(boulder.wear);
        parent.push_back(boulder.parent);
        piece.push_back(boulder.piece);
        rest.push_back(static_cast<std::uint8_t>(boulder.rest));
        tilt.push_back(boulder.tilt * kRadian);
        azimuth.push_back(boulder.tiltAzimuth * kRadian);
    }
    nb::dict table;
    table["x"] = Column(std::move(x));
    table["y"] = Column(std::move(y));
    table["ground"] = Column(std::move(ground));
    table["diameter"] = Column(std::move(diameter));
    table["height"] = Column(std::move(height));
    table["burial"] = Column(std::move(burial));
    table["yaw_deg"] = Column(std::move(yaw));
    table["lean_deg"] = Column(std::move(lean));
    table["id"] = Column(std::move(id));
    table["model"] = Column(std::move(model));
    table["wear"] = Column(std::move(wear));
    table["parent"] = Column(std::move(parent));
    table["piece"] = Column(std::move(piece));
    table["rest"] = Column(std::move(rest));
    table["tilt_deg"] = Column(std::move(tilt));
    table["tilt_azimuth_deg"] = Column(std::move(azimuth));
    return table;
}

}  // namespace terraingen::python
