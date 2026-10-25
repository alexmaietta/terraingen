// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/random/stream.h"

namespace terraingen {

RandomStream::RandomStream(std::uint32_t seed, std::uint32_t stream, std::uint32_t a,
                           std::uint32_t b, std::uint32_t c)
    : key_{seed, stream}, counter_{a, b, c, 0u} {}

std::uint32_t RandomStream::NextU32() {
    if (used_ == 4) {
        block_ = Philox4x32(counter_, key_);
        ++counter_.w;
        used_ = 0;
    }
    const std::uint32_t words[4] = {block_.x, block_.y, block_.z, block_.w};
    return words[used_++];
}

double RandomStream::NextDouble() {
    const std::uint64_t high = NextU32() >> 5u;
    const std::uint64_t low = NextU32() >> 6u;
    return static_cast<double>((high << 26u) | low) * 0x1p-53;
}

}  // namespace terraingen
