// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/layers/prefetched_ground.h"

#include "terraingen/util/bits.h"

#include <cstdint>
#include <map>

namespace terraingen {

namespace {

/// A table slot holding no read.
constexpr std::uint32_t kEmpty = 0xFFFFFFFFU;

}  // namespace

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

GroundReads::GroundReads(const Ground& ground, Span<const GroundRead> reads)
    : ground_(ground), heights_(reads.size()) {
    std::size_t capacity = 16;
    while (capacity < 2 * reads.size()) {
        capacity *= 2;
    }
    table_.assign(capacity, kEmpty);
    std::vector<std::uint32_t> places(reads.size());
    std::map<std::uint64_t, std::vector<std::uint32_t>> bySpacing;
    for (std::size_t n = 0; n < reads.size(); ++n) {
        const GroundRead& read = reads[n];
        const std::uint64_t x = BitCast<std::uint64_t>(read.point.x);
        const std::uint64_t y = BitCast<std::uint64_t>(read.point.y);
        const std::uint64_t spacing = BitCast<std::uint64_t>(read.spacing);
        std::uint32_t& slot = table_[Slot(x, y, spacing)];
        if (slot == kEmpty) {
            slot = static_cast<std::uint32_t>(distinct_.size());
            distinct_.push_back({x, y, spacing, 0.0f});
            bySpacing[spacing].push_back(slot);
        }
        places[n] = slot;
    }
    std::vector<Vec2d> points;
    std::vector<float> read;
    for (const auto& [spacing, indices] : bySpacing) {
        points.resize(indices.size());
        read.assign(indices.size(), 0.0f);
        for (std::size_t n = 0; n < indices.size(); ++n) {
            const Held& held = distinct_[indices[n]];
            points[n] = {BitCast<double>(held.x), BitCast<double>(held.y)};
        }
        ground_.HeightsAt(points, read, BitCast<double>(spacing));
        for (std::size_t n = 0; n < indices.size(); ++n) {
            distinct_[indices[n]].height = read[n];
        }
    }
    for (std::size_t n = 0; n < reads.size(); ++n) {
        heights_[n] = distinct_[places[n]].height;
    }
}

std::size_t GroundReads::Slot(std::uint64_t x, std::uint64_t y, std::uint64_t spacing) const {
    const std::size_t mask = table_.size() - 1;
    std::uint64_t hash = x ^ (y * 0x9E3779B97F4A7C15ULL) ^ (spacing * 0xC2B2AE3D27D4EB4FULL);
    hash = (hash ^ (hash >> 30U)) * 0xBF58476D1CE4E5B9ULL;
    hash = (hash ^ (hash >> 27U)) * 0x94D049BB133111EBULL;
    hash ^= hash >> 31U;
    for (std::size_t slot = hash & mask;; slot = (slot + 1) & mask) {
        const std::uint32_t held = table_[slot];
        if (held == kEmpty) {
            return slot;
        }
        const Held& entry = distinct_[held];
        if (entry.x == x && entry.y == y && entry.spacing == spacing) {
            return slot;
        }
    }
}

const float* GroundReads::Find(double x, double y, double spacing) const {
    const std::uint32_t held = table_[Slot(BitCast<std::uint64_t>(x), BitCast<std::uint64_t>(y),
                                           BitCast<std::uint64_t>(spacing))];
    return held == kEmpty ? nullptr : &distinct_[held].height;
}

const float* ReplayedGround::Held(double x, double y, double spacing) const {
    if (next_ < reads_.size()) {
        const GroundRead& recorded = reads_[next_];
        if (BitCast<std::uint64_t>(recorded.point.x) == BitCast<std::uint64_t>(x) &&
            BitCast<std::uint64_t>(recorded.point.y) == BitCast<std::uint64_t>(y) &&
            BitCast<std::uint64_t>(recorded.spacing) == BitCast<std::uint64_t>(spacing)) {
            return &heights_[next_++];
        }
    }
    return held_.Find(x, y, spacing);
}

float ReplayedGround::HeightAt(double x, double y, double spacing) const {
    const float* held = Held(x, y, spacing);
    return held != nullptr ? *held : held_.Read().HeightAt(x, y, spacing);
}

void ReplayedGround::HeightsAt(Span<const Vec2d> points, Span<float> heights,
                               double spacing) const {
    std::vector<Vec2d> missing;
    std::vector<std::size_t> where;
    for (std::size_t i = 0; i < points.size(); ++i) {
        if (const float* held = Held(points[i].x, points[i].y, spacing)) {
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
    held_.Read().HeightsAt(missing, read, spacing);
    for (std::size_t n = 0; n < missing.size(); ++n) {
        heights[where[n]] = read[n];
    }
}

}  // namespace terraingen
