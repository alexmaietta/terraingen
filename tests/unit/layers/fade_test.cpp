// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/layers/fade.h"

#include <gtest/gtest.h>

#include <cmath>
#include <stdexcept>

using terraingen::BlendShape;
using terraingen::Falloff;

TEST(Smoothstep, ClampsAndEasesBothEnds) {
    EXPECT_EQ(terraingen::Smoothstep(-1.0), 0.0);
    EXPECT_EQ(terraingen::Smoothstep(0.0), 0.0);
    EXPECT_EQ(terraingen::Smoothstep(0.5), 0.5);
    EXPECT_EQ(terraingen::Smoothstep(1.0), 1.0);
    EXPECT_EQ(terraingen::Smoothstep(3.0), 1.0);
    EXPECT_DOUBLE_EQ(terraingen::Smoothstep(0.25), 0.25 * 0.25 * (3.0 - 0.5));
}

TEST(Falloff, FallsFromOneToZeroAcrossTheBand) {
    for (const BlendShape shape : {BlendShape::Linear, BlendShape::Smoothstep,
                                   BlendShape::Smootherstep, BlendShape::Gaussian}) {
        EXPECT_EQ(Falloff(0.0, 0.8, 1.2, shape), 1.0);
        EXPECT_EQ(Falloff(0.8, 0.8, 1.2, shape), 1.0);
        EXPECT_EQ(Falloff(1.2, 0.8, 1.2, shape), 0.0);
        EXPECT_EQ(Falloff(5.0, 0.8, 1.2, shape), 0.0);
        EXPECT_NEAR(Falloff(0.8 + 1e-9, 0.8, 1.2, shape), 1.0, 1e-6);
        EXPECT_NEAR(Falloff(1.2 - 1e-9, 0.8, 1.2, shape), 0.0, 1e-6);
        double previous = 1.0;
        for (int i = 1; i < 40; ++i) {
            const double weight = Falloff(0.8 + 0.01 * i, 0.8, 1.2, shape);
            EXPECT_LE(weight, previous);
            previous = weight;
        }
    }
    EXPECT_DOUBLE_EQ(Falloff(1.0, 0.8, 1.2, BlendShape::Linear), 0.5);
    EXPECT_DOUBLE_EQ(Falloff(1.0, 0.8, 1.2, BlendShape::Smoothstep), 0.5);
    EXPECT_DOUBLE_EQ(Falloff(1.0, 0.8, 1.2, BlendShape::Smootherstep), 0.5);
    const double tail = std::exp(-4.5);
    EXPECT_NEAR(Falloff(1.0, 0.8, 1.2, BlendShape::Gaussian),
                (std::exp(-1.125) - tail) / (1.0 - tail), 1e-12);
    EXPECT_NEAR(Falloff(0.9, 0.8, 1.2, BlendShape::Linear), 0.75, 1e-12);
}

TEST(ResolutionWeight, FadesFromStartToEndSamples) {
    const terraingen::ResolutionFade fade{};
    EXPECT_EQ(terraingen::ResolutionWeight(1.0, 1.0, fade), 0.0);
    EXPECT_EQ(terraingen::ResolutionWeight(2.0, 1.0, fade), 0.0);
    EXPECT_DOUBLE_EQ(terraingen::ResolutionWeight(3.0, 1.0, fade), 0.5);
    EXPECT_EQ(terraingen::ResolutionWeight(4.0, 1.0, fade), 1.0);
    EXPECT_EQ(terraingen::ResolutionWeight(40.0, 10.0, fade), 1.0);
    double previous = 0.0;
    for (double samples = 2.0; samples <= 4.0; samples += 0.01) {
        const double weight = terraingen::ResolutionWeight(samples, 1.0, fade);
        EXPECT_GE(weight, previous);
        previous = weight;
    }
    const terraingen::ResolutionFade linear{1.0, 3.0, BlendShape::Linear};
    EXPECT_DOUBLE_EQ(terraingen::ResolutionWeight(1.5, 1.0, linear), 0.25);
    EXPECT_NO_THROW(terraingen::ValidateResolutionFade({0.0, 0.5}));
    EXPECT_THROW(terraingen::ValidateResolutionFade({3.0, 3.0}), std::invalid_argument);
    EXPECT_THROW(terraingen::ValidateResolutionFade({-1.0, 3.0}), std::invalid_argument);
}
