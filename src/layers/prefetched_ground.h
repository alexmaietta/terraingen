// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief Ground reads recorded, read up front in one pass and replayed, and `MapReadingGround`.
#pragma once

#include "terraingen/geometry/geometry.h"
#include "terraingen/layers/ground.h"
#include "terraingen/util/bits.h"
#include "terraingen/util/parallel.h"
#include "terraingen/util/span.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
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

  protected:
    void FillSamples(SampleBlock& samples, double spacing, unsigned threads) const override;

  private:
    std::vector<GroundRead>& reads_;
};

/// The heights of a ground at recorded reads, each distinct read (point and spacing, to the bit)
/// read once in one `HeightsAt` pass per spacing: every read's height in order, and any distinct
/// read's found by its point and spacing. Thread-safe.
class GroundReads {
  public:
    /// Reads `ground` at `reads` on `threads` threads (0 for every hardware thread); `ground` must
    /// outlive it.
    GroundReads(const Ground& ground, Span<const GroundRead> reads, unsigned threads = 1);

    /// The ground read.
    const Ground& Read() const {
        return ground_;
    }

    /// The height of each read, in their order.
    const std::vector<float>& Heights() const {
        return heights_;
    }

    /// The height read at (`x`, `y`) and `spacing`, or null when it was not read.
    const float* Find(double x, double y, double spacing) const;

    /// The number of distinct reads.
    std::size_t Distinct() const {
        return distinct_.size();
    }

  private:
    struct Held {
        std::uint64_t x = 0;
        std::uint64_t y = 0;
        std::uint64_t spacing = 0;
        float height = 0.0f;
    };

    /// The slot of the table where (`x`, `y`, `spacing`) is held or would go.
    std::size_t Slot(std::uint64_t x, std::uint64_t y, std::uint64_t spacing) const;

    const Ground& ground_;
    std::vector<float> heights_;
    std::vector<Held> distinct_;
    std::vector<std::uint32_t> table_;
};

/// A ground that replays reads recorded by a `RecordingGround`: a read matching the next one
/// recorded, its point and spacing to the bit, takes that read's height, any other one `held`
/// read, and the rest are read from the ground. Gives the heights of the ground, to the bit; not
/// thread-safe.
class ReplayedGround final : public Ground {
  public:
    /// Replays `reads`, which are those of `held` from `first` on; both must outlive it.
    ReplayedGround(const GroundReads& held, Span<const GroundRead> reads, std::size_t first)
        : held_(held), reads_(reads),
          heights_(Span<const float>(held.Heights()).subspan(first, reads.size())) {}

    /// The number of recorded reads replayed in order so far.
    std::size_t Replayed() const {
        return next_;
    }

  protected:
    void FillSamples(SampleBlock& samples, double spacing, unsigned threads) const override;

  private:
    /// The height of the next recorded read if it is at (`x`, `y`) and `spacing`, else of any
    /// held read there, else null.
    const float* Held(double x, double y, double spacing) const;

    const GroundReads& held_;
    Span<const GroundRead> reads_;
    Span<const float> heights_;
    mutable std::size_t next_ = 0;
};

/// Calls `store(i, value(i, ahead))` for each i in [0, `count`), where `ahead` serves the points
/// `value` reads of `ground` from one batched read, so a layer whose samples each read the ground
/// at many points reads it once per block rather than once per point.
///
/// Samples are taken in blocks: `value` first runs on a `RecordingGround`, and a sample that read
/// nothing keeps that value; every point recorded is read in one `GroundReads`, and the others run
/// again on a `ReplayedGround` of their task's reads. The results are those of `value` on `ground`
/// itself, to the bit, as long as `value` is deterministic and reads the ground only through the
/// one it is given.
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
        std::vector<std::size_t> first(tasks + 1, 0);
        for (std::size_t task = 0; task < tasks; ++task) {
            first[task + 1] = first[task] + reads[task].size();
        }
        if (first[tasks] > 0) {
            std::vector<GroundRead> all;
            all.reserve(first[tasks]);
            for (std::vector<GroundRead>& some : reads) {
                all.insert(all.end(), some.begin(), some.end());
                some = {};
            }
            const GroundReads held(ground, all, threads);
            ParallelFor(tasks, threads, [&](std::size_t task) {
                const std::size_t reads = first[task + 1] - first[task];
                const ReplayedGround replay(
                    held, Span<const GroundRead>(all).subspan(first[task], reads), first[task]);
                const std::size_t end = std::min(size, (task + 1) * kTask);
                for (std::size_t i = task * kTask; i < end; ++i) {
                    if (read[i] != 0) {
                        values[i] = value(begin + i, static_cast<const Ground&>(replay));
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
