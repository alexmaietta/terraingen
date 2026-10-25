// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/layers/prefetched_ground.h"

#include <gtest/gtest.h>

#include <atomic>
#include <cmath>
#include <cstddef>
#include <vector>

using terraingen::Ground;
using terraingen::GroundRead;
using terraingen::PrefetchedGround;
using terraingen::RecordingGround;
using terraingen::Span;
using terraingen::Vec2d;

namespace {

/// A wavy ground that depends on the spacing and counts how it is read.
class Counted final : public Ground {
  public:
    float HeightAt(double x, double y, double spacing) const override {
        ++single;
        return Value(x, y, spacing);
    }
    void HeightsAt(Span<const Vec2d> points, Span<float> heights, double spacing) const override {
        ++batches;
        batched += points.size();
        for (std::size_t i = 0; i < points.size(); ++i) {
            heights[i] = Value(points[i].x, points[i].y, spacing);
        }
    }

    static float Value(double x, double y, double spacing) {
        return static_cast<float>(std::sin(0.37 * x) * std::cos(0.21 * y) + 0.01 * spacing);
    }

    mutable std::atomic<std::size_t> single{0};
    mutable std::atomic<std::size_t> batches{0};
    mutable std::atomic<std::size_t> batched{0};
};

}  // namespace

TEST(RecordingGround, RecordsEveryReadAndGivesZero) {
    std::vector<GroundRead> reads;
    const RecordingGround recording(reads);
    EXPECT_EQ(recording.HeightAt(1.0, 2.0, 0.5), 0.0f);
    const std::vector<Vec2d> points{{3.0, 4.0}, {5.0, 6.0}};
    std::vector<float> heights(2, 7.0f);
    recording.HeightsAt(points, heights, 2.0);
    EXPECT_EQ(heights, (std::vector<float>{0.0f, 0.0f}));
    ASSERT_EQ(reads.size(), 3u);
    EXPECT_EQ(reads[0].point, (Vec2d{1.0, 2.0}));
    EXPECT_EQ(reads[0].spacing, 0.5);
    EXPECT_EQ(reads[2].point, (Vec2d{5.0, 6.0}));
    EXPECT_EQ(reads[2].spacing, 2.0);
}

TEST(PrefetchedGround, ReadsEachDistinctPointOncePerSpacingAndFallsBackForOthers) {
    const Counted ground;
    std::vector<GroundRead> reads{{{1.0, 2.0}, 1.0}, {{3.0, -4.0}, 1.0}, {{1.0, 2.0}, 1.0},
                                  {{1.0, 2.0}, 2.0}, {{0.0, 0.0}, 1.0},  {{-0.0, 0.0}, 1.0}};
    const PrefetchedGround ahead(ground, reads);
    EXPECT_EQ(ahead.Size(), 5u) << "duplicates are read once; -0 and 0 are distinct reads";
    EXPECT_EQ(ground.batches.load(), 2u) << "one batch per spacing";
    EXPECT_EQ(ground.batched.load(), 5u);
    EXPECT_EQ(ground.single.load(), 0u);
    for (const GroundRead& read : reads) {
        EXPECT_EQ(ahead.HeightAt(read.point.x, read.point.y, read.spacing),
                  Counted::Value(read.point.x, read.point.y, read.spacing));
    }
    EXPECT_EQ(ground.single.load(), 0u) << "held reads do not reach the ground";
    EXPECT_EQ(ahead.HeightAt(3.0, -4.0, 2.0), Counted::Value(3.0, -4.0, 2.0));
    EXPECT_EQ(ahead.HeightAt(9.0, 9.0, 1.0), Counted::Value(9.0, 9.0, 1.0));
    EXPECT_EQ(ground.single.load(), 2u) << "another spacing or point is read from the ground";
    const std::vector<Vec2d> points{{1.0, 2.0}, {7.0, 8.0}, {3.0, -4.0}, {-7.0, 1.0}};
    std::vector<float> heights(points.size());
    ahead.HeightsAt(points, heights, 1.0);
    for (std::size_t i = 0; i < points.size(); ++i) {
        EXPECT_EQ(heights[i], Counted::Value(points[i].x, points[i].y, 1.0));
    }
    EXPECT_EQ(ground.batches.load(), 3u) << "the misses are read in one batch";
    EXPECT_EQ(ground.batched.load(), 7u);
}

TEST(PrefetchedGround, HoldsNothingWithoutReads) {
    const Counted ground;
    const PrefetchedGround ahead(ground, {});
    EXPECT_EQ(ahead.Size(), 0u);
    EXPECT_EQ(ground.batches.load(), 0u);
    EXPECT_EQ(ahead.HeightAt(1.0, 1.0, 1.0), Counted::Value(1.0, 1.0, 1.0));
}

TEST(MapReadingGround, GivesTheValuesOfReadingTheGroundDirectly) {
    const Counted ground;
    constexpr std::size_t kCount = 40000;
    const auto value = [](std::size_t i, const Ground& on) {
        const double x = 0.5 * static_cast<double>(i % 200);
        const double y = 0.5 * static_cast<double>(i / 200);
        if (i % 3 == 0) {
            return static_cast<float>(x - y);
        }
        double sum = 0.0;
        for (int k = 0; k < 8; ++k) {
            const double angle = 0.785 * k;
            sum += static_cast<double>(
                on.HeightAt(x + 3.0 * std::cos(angle), y + 3.0 * std::sin(angle), 1.0));
        }
        if (i % 3 == 1 && on.HeightAt(x, y, 1.0) > 0.0f) {
            sum += static_cast<double>(on.HeightAt(x + 0.25, y, 0.5));
        }
        return static_cast<float>(sum);
    };
    std::vector<float> direct(kCount);
    for (std::size_t i = 0; i < kCount; ++i) {
        direct[i] = value(i, ground);
    }
    ground.single = 0;
    for (const unsigned threads : {1u, 0u}) {
        std::vector<float> mapped(kCount, -1.0f);
        terraingen::MapReadingGround<float>(kCount, threads, ground, value,
                                            [&](std::size_t i, float v) { mapped[i] = v; });
        for (std::size_t i = 0; i < kCount; ++i) {
            ASSERT_EQ(mapped[i], direct[i]) << "sample " << i << " on " << threads << " threads";
        }
    }
    EXPECT_GT(ground.single.load(), 0u)
        << "reads that depend on heights read are not recorded and fall back to the ground";
    EXPECT_LT(ground.single.load(), kCount) << "every recorded read is served up front";
}

TEST(MapReadingGround, EvaluatesSamplesThatReadNothingOnce) {
    const Counted ground;
    std::atomic<std::size_t> calls{0};
    std::vector<double> stored(1000);
    terraingen::MapReadingGround<double>(
        stored.size(), 0, ground,
        [&](std::size_t i, const Ground& /*on*/) {
            ++calls;
            return 2.0 * static_cast<double>(i);
        },
        [&](std::size_t i, double v) { stored[i] = v; });
    EXPECT_EQ(calls.load(), stored.size());
    EXPECT_EQ(ground.batches.load() + ground.single.load(), 0u);
    for (std::size_t i = 0; i < stored.size(); ++i) {
        EXPECT_EQ(stored[i], 2.0 * static_cast<double>(i));
    }
}
