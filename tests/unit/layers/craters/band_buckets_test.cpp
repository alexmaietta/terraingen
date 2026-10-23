// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/layers/craters/band_buckets.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <vector>

using terraingen::BandBuckets;
using terraingen::Surface;

namespace {

/// The indices `buckets` visits around `point` in band `band`, in the order visited.
std::vector<std::size_t> Around(const BandBuckets& buckets, int band, double x, double y,
                                long long span) {
    std::vector<std::size_t> visited;
    buckets.ForEachAround(band, {x, y}, span, [&](const std::vector<std::size_t>& bucket) {
        visited.insert(visited.end(), bucket.begin(), bucket.end());
    });
    return visited;
}

}  // namespace

TEST(BandBuckets, FindsTheIndicesInTheBucketsAroundAPointRowsFirst) {
    const Surface plane;
    BandBuckets buckets(plane);
    buckets.Add(0, 10.0, {5.0, 5.0}, 0);
    buckets.Add(0, 99.0, {15.0, 5.0}, 1);
    buckets.Add(0, 10.0, {5.0, 15.0}, 2);
    buckets.Add(0, 10.0, {5.0, 5.0}, 3);
    buckets.Add(0, 10.0, {45.0, 5.0}, 4);
    buckets.Add(2, 40.0, {5.0, 5.0}, 5);
    EXPECT_EQ(buckets.Bands(), (std::vector<int>{0, 2}));
    EXPECT_TRUE(buckets.Has(2));
    EXPECT_FALSE(buckets.Has(1));
    EXPECT_EQ(buckets.Size(0), 10.0) << "a band keeps its first size";
    EXPECT_EQ(Around(buckets, 0, 6.0, 6.0, 1), (std::vector<std::size_t>{0, 3, 1, 2}));
    EXPECT_EQ(Around(buckets, 0, 6.0, 6.0, 0), (std::vector<std::size_t>{0, 3}));
    EXPECT_EQ(Around(buckets, 0, 6.0, 6.0, 4), (std::vector<std::size_t>{0, 3, 1, 4, 2}));
    EXPECT_EQ(Around(buckets, 2, 6.0, 6.0, 1), (std::vector<std::size_t>{5}));
    EXPECT_TRUE(Around(buckets, 1, 6.0, 6.0, 1).empty());
}

TEST(BandBuckets, SearchesTheLayersEitherSideOnABody) {
    const Surface moon = Surface::Sphere(1737400.0);
    BandBuckets buckets(moon);
    buckets.Add(0, 1000.0, {10.0, 20.0}, 7);
    EXPECT_EQ(Around(buckets, 0, 10.0, 20.0, 1), (std::vector<std::size_t>{7}));
    EXPECT_TRUE(Around(buckets, 0, -170.0, -20.0, 1).empty());
}
