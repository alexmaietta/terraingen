#include "terraingen/util/bits.h"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstdint>

TEST(Bits, BitCastRoundTripsADouble) {
    const double value = -1234.5678e-9;
    const auto bits = terraingen::BitCast<std::uint64_t>(value);
    EXPECT_EQ(terraingen::BitCast<double>(bits), value);
    EXPECT_EQ(terraingen::BitCast<std::uint64_t>(1.0), 0x3FF0000000000000ull);
    EXPECT_EQ(terraingen::BitCast<std::uint32_t>(1.0f), 0x3F800000u);
}

TEST(Bits, PiMatchesItsDefinition) {
    EXPECT_DOUBLE_EQ(terraingen::kPi, std::acos(-1.0));
}

TEST(Bits, KnowsTheByteOrder) {
    const std::uint16_t one = 1u;
    const auto lowByte = terraingen::BitCast<std::array<std::uint8_t, 2>>(one)[0];
    EXPECT_EQ(terraingen::kLittleEndian, lowByte == 1u);
}
