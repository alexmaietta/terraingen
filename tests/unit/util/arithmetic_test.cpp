// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/util/bits.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>

TEST(Arithmetic, CeilDivRoundsUp) {
    EXPECT_EQ(terraingen::CeilDiv<std::size_t>(0, 4), 0u);
    EXPECT_EQ(terraingen::CeilDiv<std::size_t>(1, 4), 1u);
    EXPECT_EQ(terraingen::CeilDiv<std::size_t>(8, 4), 2u);
    EXPECT_EQ(terraingen::CeilDiv<std::size_t>(9, 4), 3u);
    static_assert(terraingen::CeilDiv<std::uint32_t>(257, 256) == 2u);
}

TEST(Arithmetic, ClampIndexKeepsIndicesInRange) {
    EXPECT_EQ(terraingen::ClampIndex(-3.0, 5), 0u);
    EXPECT_EQ(terraingen::ClampIndex(2.0, 5), 2u);
    EXPECT_EQ(terraingen::ClampIndex(4.0, 5), 4u);
    EXPECT_EQ(terraingen::ClampIndex(9.0, 5), 4u);
    EXPECT_EQ(terraingen::ClampIndex(7.0, 1), 0u);
    static_assert(terraingen::ClampIndex(1.0, 3) == 1u);
}
