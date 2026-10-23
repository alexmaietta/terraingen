#include "terraingen/layers/boulders/ejecta_index.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

using terraingen::Crater;
using terraingen::CraterList;
using terraingen::EjectaIndex;
using terraingen::Surface;

namespace {

/// Craters of many sizes scattered over a few hundred meters.
CraterList Scattered() {
    CraterList craters;
    for (int n = 0; n < 400; ++n) {
        Crater crater;
        crater.x = std::fmod(n * 37.3, 300.0) - 150.0;
        crater.y = std::fmod(n * 91.7, 300.0) - 150.0;
        crater.diameter = 1.0 + std::fmod(n * 13.1, 40.0);
        craters.Append(crater);
    }
    return craters;
}

double Reach(const Crater& crater) {
    return 1.5 * crater.diameter;
}

}  // namespace

TEST(EjectaIndex, FindsEveryCraterThatReachesAPointInOrder) {
    const CraterList craters = Scattered();
    const EjectaIndex index(craters, Surface::Plane(), Reach);
    for (double y = -220.0; y <= 220.0; y += 7.3) {
        for (double x = -220.0; x <= 220.0; x += 7.3) {
            const std::vector<std::uint32_t> near = index.Near(x, y);
            EXPECT_TRUE(std::is_sorted(near.begin(), near.end()));
            for (std::size_t n = 0; n < craters.Size(); ++n) {
                const Crater crater = craters.At(n);
                if (std::hypot(crater.x - x, crater.y - y) < Reach(crater)) {
                    EXPECT_TRUE(std::binary_search(near.begin(), near.end(), n))
                        << "crater " << n << " reaches (" << x << ", " << y << ")";
                }
            }
        }
    }
    EXPECT_TRUE(index.Near(1e4, 0.0).empty()) << "beyond every reach";
    EXPECT_LT(index.Near(0.0, 0.0).size(), craters.Size());
}

TEST(EjectaIndex, FindsEveryCraterThatReachesAPointOnABody) {
    const Surface moon = Surface::Sphere(1737400.0);
    CraterList craters;
    const CraterList plane = Scattered();
    for (std::size_t n = 0; n < plane.Size(); ++n) {
        Crater crater = plane.At(n);
        crater.x /= 3000.0;
        crater.y /= 3000.0;
        crater.diameter *= 3.0;
        craters.Append(crater);
    }
    const EjectaIndex index(craters, moon, Reach);
    for (double y = -0.07; y <= 0.07; y += 0.0031) {
        for (double x = -0.07; x <= 0.07; x += 0.0031) {
            const std::vector<std::uint32_t> near = index.Near(x, y);
            for (std::size_t n = 0; n < craters.Size(); ++n) {
                const Crater crater = craters.At(n);
                if (moon.Distance({crater.x, crater.y}, {x, y}) < Reach(crater)) {
                    EXPECT_TRUE(std::binary_search(near.begin(), near.end(), n));
                }
            }
        }
    }
    EXPECT_LT(index.Near(0.0, 0.0).size(), craters.Size());
    EXPECT_TRUE(EjectaIndex().Empty());
    EXPECT_TRUE(EjectaIndex().Near(0.0, 0.0).empty());
}
