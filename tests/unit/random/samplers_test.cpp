#include "terraingen/random/samplers.h"
#include "terraingen/random/stream.h"

#include <gtest/gtest.h>

#include <limits>
#include <stdexcept>

using terraingen::RandomStream;
using terraingen::SamplePoisson;

TEST(SamplePoisson, HandlesZeroAndRejectsInvalidMeans) {
    RandomStream stream(1u, 1u);
    EXPECT_EQ(SamplePoisson(stream, 0.0), 0u);
    EXPECT_THROW(SamplePoisson(stream, -1.0), std::invalid_argument);
    EXPECT_THROW(SamplePoisson(stream, std::numeric_limits<double>::quiet_NaN()),
                 std::invalid_argument);
    EXPECT_THROW(SamplePoisson(stream, std::numeric_limits<double>::infinity()),
                 std::invalid_argument);
    EXPECT_THROW(SamplePoisson(stream, 2e9), std::invalid_argument);
}
