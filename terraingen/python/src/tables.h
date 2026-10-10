#pragma once

#include "terraingen/layers/boulders/boulder.h"
#include "terraingen/layers/craters/attributes.h"
#include "terraingen/layers/craters/population.h"

#include <nanobind/nanobind.h>
#include <nanobind/ndarray.h>

#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

namespace terraingen::python {

namespace nb = nanobind;

/// A NumPy array owning a copy of `values`.
template <typename T> inline nb::object Column(std::vector<T> values) {
    auto* owned = new std::vector<T>(std::move(values));
    nb::capsule owner(owned, [](void* p) noexcept { delete static_cast<std::vector<T>*>(p); });
    return nb::cast(nb::ndarray<nb::numpy, T, nb::ndim<1>>(owned->data(), {owned->size()}, owner));
}

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

inline nb::dict BoulderTable(const std::vector<Boulder>& boulders) {
    std::vector<double> x, y, ground, diameter, height, burial, yaw, lean;
    std::vector<std::uint64_t> id;
    std::vector<std::uint8_t> model;
    for (const Boulder& boulder : boulders) {
        x.push_back(boulder.x);
        y.push_back(boulder.y);
        ground.push_back(boulder.ground.value_or(std::numeric_limits<double>::quiet_NaN()));
        diameter.push_back(boulder.diameter);
        height.push_back(boulder.height);
        burial.push_back(boulder.burial);
        yaw.push_back(boulder.yaw);
        lean.push_back(boulder.lean);
        id.push_back(boulder.id);
        model.push_back(boulder.model ? 1 : 0);
    }
    nb::dict table;
    table["x"] = Column(std::move(x));
    table["y"] = Column(std::move(y));
    table["ground"] = Column(std::move(ground));
    table["diameter"] = Column(std::move(diameter));
    table["height"] = Column(std::move(height));
    table["burial"] = Column(std::move(burial));
    table["yaw"] = Column(std::move(yaw));
    table["lean"] = Column(std::move(lean));
    table["id"] = Column(std::move(id));
    table["model"] = Column(std::move(model));
    return table;
}

}  // namespace terraingen::python
