// SPDX-License-Identifier: BSD-3-Clause

#include "layers/prefetched_ground.h"

#include <gtest/gtest.h>

#include <atomic>
#include <cmath>
#include <cstddef>
#include <vector>

using terraingen::Ground;
using terraingen::GroundRead;
using terraingen::GroundReads;
using terraingen::RecordingGround;
using terraingen::ReplayedGround;
using terraingen::Span;
using terraingen::Vec2d;

namespace {

/// A wavy ground that depends on the spacing and counts how it is read.
class Counted final : public Ground {
  public:
    static float Value(double x, double y, double spacing) {
        return static_cast<float>(std::sin(0.37 * x) * std::cos(0.21 * y) + 0.01 * spacing);
    }

    mutable std::atomic<std::size_t> batches{0};  ///< Reads, each of one or more points.
    mutable std::atomic<std::size_t> batched{0};  ///< Points read.

  protected:
    void FillSamples(terraingen::SampleBlock& samples, double spacing,
                     unsigned /*threads*/) const override {
        ++batches;
        batched += samples.Size();
        for (std::size_t i = 0; i < samples.Size(); ++i) {
            const Vec2d p = samples.Position(i);
            samples.Height(i) = Value(p.x, p.y, spacing);
        }
    }
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

TEST(GroundReads, ReadsEachDistinctReadOnceInOneBatchPerSpacing) {
    const Counted ground;
    const std::vector<GroundRead> reads{{{1.0, 2.0}, 1.0},  {{3.0, -4.0}, 1.0}, {{1.0, 2.0}, 2.0},
                                        {{0.0, 0.0}, 1.0},  {{1.0, 2.0}, 1.0},  {{-0.0, 0.0}, 1.0},
                                        {{3.0, -4.0}, 1.0}, {{1.0, 2.0}, 2.0}};
    const GroundReads held(ground, reads);
    ASSERT_EQ(held.Heights().size(), reads.size());
    for (std::size_t i = 0; i < reads.size(); ++i) {
        const GroundRead& read = reads[i];
        EXPECT_EQ(held.Heights()[i], Counted::Value(read.point.x, read.point.y, read.spacing));
        ASSERT_NE(held.Find(read.point.x, read.point.y, read.spacing), nullptr);
        EXPECT_EQ(*held.Find(read.point.x, read.point.y, read.spacing), held.Heights()[i]);
    }
    EXPECT_EQ(held.Distinct(), 5u) << "-0 and 0 are distinct reads";
    EXPECT_EQ(ground.batches.load(), 2u) << "one batch per spacing";
    EXPECT_EQ(ground.batched.load(), 5u);
    EXPECT_EQ(held.Find(3.0, -4.0, 2.0), nullptr);
    EXPECT_EQ(held.Find(9.0, 9.0, 1.0), nullptr);
    EXPECT_EQ(&held.Read(), &ground);
    const GroundReads none(ground, {});
    EXPECT_TRUE(none.Heights().empty());
    EXPECT_EQ(none.Find(1.0, 2.0, 1.0), nullptr);
    EXPECT_EQ(ground.batches.load(), 2u);
}

TEST(ReplayedGround, ReplaysReadsInOrderFindsOthersHeldAndReadsTheRest) {
    const Counted ground;
    const std::vector<GroundRead> reads{{{1.0, 2.0}, 1.0},
                                        {{3.0, -4.0}, 1.0},
                                        {{5.0, 6.0}, 1.0},
                                        {{7.0, 8.0}, 1.0},
                                        {{0.0, 0.0}, 1.0}};
    const GroundReads held(ground, reads);
    const ReplayedGround replay(held, Span<const GroundRead>(reads).subspan(1), 1);
    ground.batches = 0;
    EXPECT_EQ(replay.HeightAt(3.0, -4.0, 1.0), Counted::Value(3.0, -4.0, 1.0));
    EXPECT_EQ(replay.Replayed(), 1u);
    EXPECT_EQ(replay.HeightAt(1.0, 2.0, 1.0), Counted::Value(1.0, 2.0, 1.0))
        << "held, though out of order";
    EXPECT_EQ(replay.Replayed(), 1u) << "a read out of order leaves the replay where it is";
    EXPECT_EQ(ground.batches.load(), 0u);
    EXPECT_EQ(replay.HeightAt(3.0, -4.0, 2.0), Counted::Value(3.0, -4.0, 2.0));
    EXPECT_EQ(ground.batches.load(), 1u) << "another spacing is read from the ground";
    const std::vector<Vec2d> points{{5.0, 6.0}, {9.0, 9.0}, {7.0, 8.0}};
    std::vector<float> heights(points.size());
    replay.HeightsAt(points, heights, 1.0);
    for (std::size_t i = 0; i < points.size(); ++i) {
        EXPECT_EQ(heights[i], Counted::Value(points[i].x, points[i].y, 1.0));
    }
    EXPECT_EQ(ground.batches.load(), 2u) << "the misses are read in one batch";
    EXPECT_EQ(ground.batched.load(), reads.size() + 2);
    EXPECT_EQ(replay.Replayed(), 3u);
    EXPECT_EQ(replay.HeightAt(-0.0, 0.0, 1.0), Counted::Value(-0.0, 0.0, 1.0));
    EXPECT_EQ(ground.batches.load(), 3u) << "-0 is not the recorded 0";
    EXPECT_EQ(replay.HeightAt(0.0, 0.0, 1.0), Counted::Value(0.0, 0.0, 1.0));
    EXPECT_EQ(replay.Replayed(), 4u);
    EXPECT_EQ(replay.HeightAt(0.0, 0.0, 1.0), Counted::Value(0.0, 0.0, 1.0));
    EXPECT_EQ(ground.batches.load(), 3u) << "past the last recorded read, still held";
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
    ground.batches = 0;
    for (const unsigned threads : {1u, 0u}) {
        std::vector<float> mapped(kCount, -1.0f);
        terraingen::MapReadingGround<float>(kCount, threads, ground, value,
                                            [&](std::size_t i, float v) { mapped[i] = v; });
        for (std::size_t i = 0; i < kCount; ++i) {
            ASSERT_EQ(mapped[i], direct[i]) << "sample " << i << " on " << threads << " threads";
        }
    }
    EXPECT_GT(ground.batches.load(), 0u)
        << "reads that depend on heights read are not recorded and fall back to the ground";
    EXPECT_LT(ground.batches.load(), kCount / 2) << "every recorded read is served up front";
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
    EXPECT_EQ(ground.batches.load(), 0u);
    for (std::size_t i = 0; i < stored.size(); ++i) {
        EXPECT_EQ(stored[i], 2.0 * static_cast<double>(i));
    }
}
