// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/random/stream.h"

namespace terraingen {

RandomStream::RandomStream(std::uint32_t seed, std::uint32_t stream, std::uint32_t a,
                           std::uint32_t b, std::uint32_t c)
    : m_key{seed, stream}, m_counter{a, b, c, 0u} {}

std::uint32_t RandomStream::NextU32() {
    if (m_used == 4) {
        m_block = Philox4x32(m_counter, m_key);
        ++m_counter.w;
        m_used = 0;
    }
    const std::uint32_t words[4] = {m_block.x, m_block.y, m_block.z, m_block.w};
    return words[m_used++];
}

double RandomStream::NextDouble() {
    const std::uint64_t high = NextU32() >> 5u;
    const std::uint64_t low = NextU32() >> 6u;
    return static_cast<double>((high << 26u) | low) * 0x1p-53;
}

}  // namespace terraingen
