// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief `PrefetchedGround`, ground heights read up front in one pass, and `MapReadingGround`.
#pragma once

#include "terraingen/layers/ground.h"
#include "terraingen/util/arithmetic.h"
#include "terraingen/util/geometry.h"
#include "terraingen/util/parallel.h"
#include "terraingen/util/span.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace terraingen {

/// One read of a ground: a point and the sample spacing it is read at.
struct GroundRead {
    Vec2d point;
    double spacing = 0.0;
};

/// A ground that records where it is read and gives 0 m everywhere; not thread-safe.
class RecordingGround final : public Ground {
  public:
    /// A ground recording into `reads`, which must outlive it.
    explicit RecordingGround(std::vector<GroundRead>& reads) : reads_(reads) {}

    float HeightAt(double x, double y, double spacing) const override;
    void HeightsAt(Span<const Vec2d> points, Span<float> heights, double spacing) const override;

  private:
    std::vector<GroundRead>& reads_;
};

/// A ground that holds the heights of `ground` at given reads, taken up front in one `HeightsAt`
/// pass per spacing, and reads any other point or spacing from `ground`. Gives the same heights as
/// `ground`, to the bit, and is thread-safe when `ground` is.
class PrefetchedGround final : public Ground {
  public:
    /// Reads `ground` at each of `reads`; `ground` must outlive it.
    PrefetchedGround(const Ground& ground, std::vector<GroundRead> reads);

    float HeightAt(double x, double y, double spacing) const override;
    void HeightsAt(Span<const Vec2d> points, Span<float> heights, double spacing) const override;

    /// The number of distinct reads held.
    std::size_t Size() const {
        return held_.size();
    }

  private:
    struct Held {
        std::uint64_t spacing = 0;
        std::uint64_t x = 0;
        std::uint64_t y = 0;
        float height = 0.0f;

        bool operator<(const Held& other) const {
            return spacing != other.spacing ? spacing < other.spacing
                   : x != other.x           ? x < other.x
                                            : y < other.y;
        }
        bool operator==(const Held& other) const {
            return spacing == other.spacing && x == other.x && y == other.y;
        }
    };

    /// The held height at (`x`, `y`) and `spacing`, or null when not held.
    const float* Find(double x, double y, double spacing) const;

    const Ground& ground_;
    std::vector<Held> held_;
};

/// Calls `store(i, value(i, ahead))` for each i in [0, `count`), where `ahead` serves the points
/// `value` reads of `ground` from one batched read, so a layer whose samples each read the ground
/// at many points reads it once per block rather than once per point.
///
/// Samples are taken in blocks: `value` first runs on a `RecordingGround`, and a sample that read
/// nothing keeps that value; the others run again on a `PrefetchedGround` of every point recorded.
/// The results are those of `value` on `ground` itself, to the bit, as long as `value` reads the
/// ground only through the one it is given and is deterministic.
/// @param threads Threads to split each block over; 0 for every hardware thread.
template <typename T, typename Value, typename Store>
void MapReadingGround(std::size_t count, unsigned threads, const Ground& ground, const Value& value,
                      const Store& store) {
    constexpr std::size_t kBlock = std::size_t{1} << 14U;
    constexpr std::size_t kTask = 256;
    std::vector<T> values;
    std::vector<unsigned char> read;
    for (std::size_t begin = 0; begin < count; begin += kBlock) {
        const std::size_t size = std::min(kBlock, count - begin);
        const std::size_t tasks = CeilDiv(size, kTask);
        values.assign(size, T{});
        read.assign(size, 0);
        std::vector<std::vector<GroundRead>> reads(tasks);
        ParallelFor(tasks, threads, [&](std::size_t task) {
            const RecordingGround recording(reads[task]);
            const std::size_t end = std::min(size, (task + 1) * kTask);
            for (std::size_t i = task * kTask; i < end; ++i) {
                const std::size_t before = reads[task].size();
                values[i] = value(begin + i, static_cast<const Ground&>(recording));
                read[i] = reads[task].size() != before ? 1 : 0;
            }
        });
        std::vector<GroundRead> all;
        for (std::vector<GroundRead>& some : reads) {
            all.insert(all.end(), some.begin(), some.end());
            some = {};
        }
        if (!all.empty()) {
            const PrefetchedGround ahead(ground, std::move(all));
            ParallelFor(tasks, threads, [&](std::size_t task) {
                const std::size_t end = std::min(size, (task + 1) * kTask);
                for (std::size_t i = task * kTask; i < end; ++i) {
                    if (read[i] != 0) {
                        values[i] = value(begin + i, static_cast<const Ground&>(ahead));
                    }
                }
            });
        }
        for (std::size_t i = 0; i < size; ++i) {
            store(begin + i, values[i]);
        }
    }
}

}  // namespace terraingen
