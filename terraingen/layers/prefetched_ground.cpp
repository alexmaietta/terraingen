// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/layers/prefetched_ground.h"

#include "terraingen/util/bits.h"

#include <algorithm>

namespace terraingen {

float RecordingGround::HeightAt(double x, double y, double spacing) const {
    reads_.push_back({{x, y}, spacing});
    return 0.0f;
}

void RecordingGround::HeightsAt(Span<const Vec2d> points, Span<float> heights,
                                double spacing) const {
    for (std::size_t i = 0; i < points.size(); ++i) {
        reads_.push_back({points[i], spacing});
        heights[i] = 0.0f;
    }
}

PrefetchedGround::PrefetchedGround(const Ground& ground, std::vector<GroundRead> reads)
    : ground_(ground) {
    held_.reserve(reads.size());
    for (const GroundRead& read : reads) {
        held_.push_back({BitCast<std::uint64_t>(read.spacing), BitCast<std::uint64_t>(read.point.x),
                         BitCast<std::uint64_t>(read.point.y), 0.0f});
    }
    reads = {};
    std::sort(held_.begin(), held_.end());
    held_.erase(std::unique(held_.begin(), held_.end()), held_.end());
    std::vector<Vec2d> points;
    std::vector<float> heights;
    for (std::size_t begin = 0; begin < held_.size();) {
        std::size_t end = begin;
        while (end < held_.size() && held_[end].spacing == held_[begin].spacing) {
            ++end;
        }
        points.clear();
        for (std::size_t n = begin; n < end; ++n) {
            points.push_back({BitCast<double>(held_[n].x), BitCast<double>(held_[n].y)});
        }
        heights.assign(points.size(), 0.0f);
        ground_.HeightsAt(points, heights, BitCast<double>(held_[begin].spacing));
        for (std::size_t n = begin; n < end; ++n) {
            held_[n].height = heights[n - begin];
        }
        begin = end;
    }
}

const float* PrefetchedGround::Find(double x, double y, double spacing) const {
    const Held key{BitCast<std::uint64_t>(spacing), BitCast<std::uint64_t>(x),
                   BitCast<std::uint64_t>(y), 0.0f};
    const auto found = std::lower_bound(held_.begin(), held_.end(), key);
    if (found == held_.end() || !(*found == key)) {
        return nullptr;
    }
    return &found->height;
}

float PrefetchedGround::HeightAt(double x, double y, double spacing) const {
    const float* held = Find(x, y, spacing);
    return held != nullptr ? *held : ground_.HeightAt(x, y, spacing);
}

void PrefetchedGround::HeightsAt(Span<const Vec2d> points, Span<float> heights,
                                 double spacing) const {
    std::vector<Vec2d> missing;
    std::vector<std::size_t> where;
    for (std::size_t i = 0; i < points.size(); ++i) {
        if (const float* held = Find(points[i].x, points[i].y, spacing)) {
            heights[i] = *held;
        } else {
            missing.push_back(points[i]);
            where.push_back(i);
        }
    }
    if (missing.empty()) {
        return;
    }
    std::vector<float> read(missing.size());
    ground_.HeightsAt(missing, read, spacing);
    for (std::size_t n = 0; n < missing.size(); ++n) {
        heights[where[n]] = read[n];
    }
}

}  // namespace terraingen
