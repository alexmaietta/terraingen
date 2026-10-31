// SPDX-License-Identifier: BSD-3-Clause

#include "layers/prefetched_ground.h"

#include "terraingen/util/bits.h"

#include <cstdint>
#include <map>

namespace terraingen {

namespace {

/// A table slot holding no read.
constexpr std::uint32_t kEmpty = 0xFFFFFFFFU;

}  // namespace

void RecordingGround::FillSamples(SampleBlock& samples, double spacing,
                                  unsigned /*threads*/) const {
    for (std::size_t i = 0; i < samples.Size(); ++i) {
        m_reads.push_back({samples.Position(i), spacing});
        samples.Height(i) = 0.0f;
    }
}

GroundReads::GroundReads(const Ground& ground, Span<const GroundRead> reads, unsigned threads)
    : m_ground(ground), m_heights(reads.size()) {
    std::size_t capacity = 16;
    while (capacity < 2 * reads.size()) {
        capacity *= 2;
    }
    m_table.assign(capacity, kEmpty);
    std::vector<std::uint32_t> places(reads.size());
    std::map<std::uint64_t, std::vector<std::uint32_t>> bySpacing;
    for (std::size_t n = 0; n < reads.size(); ++n) {
        const GroundRead& read = reads[n];
        const std::uint64_t x = BitCast<std::uint64_t>(read.point.x);
        const std::uint64_t y = BitCast<std::uint64_t>(read.point.y);
        const std::uint64_t spacing = BitCast<std::uint64_t>(read.spacing);
        std::uint32_t& slot = m_table[Slot(x, y, spacing)];
        if (slot == kEmpty) {
            slot = static_cast<std::uint32_t>(m_distinct.size());
            m_distinct.push_back({x, y, spacing, 0.0f});
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
            const Held& held = m_distinct[indices[n]];
            points[n] = {BitCast<double>(held.x), BitCast<double>(held.y)};
        }
        m_ground.HeightsAt(points, read, BitCast<double>(spacing), threads);
        for (std::size_t n = 0; n < indices.size(); ++n) {
            m_distinct[indices[n]].height = read[n];
        }
    }
    for (std::size_t n = 0; n < reads.size(); ++n) {
        m_heights[n] = m_distinct[places[n]].height;
    }
}

std::size_t GroundReads::Slot(std::uint64_t x, std::uint64_t y, std::uint64_t spacing) const {
    const std::size_t mask = m_table.size() - 1;
    std::uint64_t hash = x ^ (y * 0x9E3779B97F4A7C15ULL) ^ (spacing * 0xC2B2AE3D27D4EB4FULL);
    hash = (hash ^ (hash >> 30U)) * 0xBF58476D1CE4E5B9ULL;
    hash = (hash ^ (hash >> 27U)) * 0x94D049BB133111EBULL;
    hash ^= hash >> 31U;
    for (std::size_t slot = hash & mask;; slot = (slot + 1) & mask) {
        const std::uint32_t held = m_table[slot];
        if (held == kEmpty) {
            return slot;
        }
        const Held& entry = m_distinct[held];
        if (entry.x == x && entry.y == y && entry.spacing == spacing) {
            return slot;
        }
    }
}

const float* GroundReads::Find(double x, double y, double spacing) const {
    const std::uint32_t held = m_table[Slot(BitCast<std::uint64_t>(x), BitCast<std::uint64_t>(y),
                                            BitCast<std::uint64_t>(spacing))];
    return held == kEmpty ? nullptr : &m_distinct[held].height;
}

const float* ReplayedGround::Held(double x, double y, double spacing) const {
    if (m_next < m_reads.size()) {
        const GroundRead& recorded = m_reads[m_next];
        if (BitCast<std::uint64_t>(recorded.point.x) == BitCast<std::uint64_t>(x) &&
            BitCast<std::uint64_t>(recorded.point.y) == BitCast<std::uint64_t>(y) &&
            BitCast<std::uint64_t>(recorded.spacing) == BitCast<std::uint64_t>(spacing)) {
            return &m_heights[m_next++];
        }
    }
    return m_held.Find(x, y, spacing);
}

void ReplayedGround::FillSamples(SampleBlock& samples, double spacing, unsigned threads) const {
    std::vector<Vec2d> missing;
    std::vector<std::size_t> where;
    for (std::size_t i = 0; i < samples.Size(); ++i) {
        const Vec2d point = samples.Position(i);
        if (const float* held = Held(point.x, point.y, spacing)) {
            samples.Height(i) = *held;
        } else {
            missing.push_back(point);
            where.push_back(i);
        }
    }
    if (missing.empty()) {
        return;
    }
    std::vector<float> read(missing.size());
    m_held.Read().HeightsAt(missing, read, spacing, threads);
    for (std::size_t n = 0; n < missing.size(); ++n) {
        samples.Height(where[n]) = read[n];
    }
}

}  // namespace terraingen
