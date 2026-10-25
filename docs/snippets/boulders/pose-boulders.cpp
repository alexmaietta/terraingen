// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief Pose Boulders by Height, Burial, Lean and Rest
///
/// Each boulder's height over its diameter, buried share and lean are drawn between bounds. A
/// share of the boulders rest on a side, their intermediate axis up, or stand on an end, their long
/// axis up, rather than lying on their broadest face. Boulder models tip over until they rest
/// stably unless settling is off.
#include "snippet_test.h"
#include "terraingen/terraingen.h"

namespace terraingen {
namespace {

TEST_F(SnippetTest, PoseBoulders) {
    BoulderLayerOptions options;
    options.seed = 4;
    options.sizes.minDiameter = 0.3;

    // --8<-- [start:pose_boulders]
    BoulderPose& pose = options.pose;
    pose.minHeightRatio = 0.3;  // height over diameter
    pose.maxHeightRatio = 0.5;
    pose.minBurial = 0.2;  // buried share of the height
    pose.maxBurial = 0.5;
    pose.maxLeanDegrees = 25.0;
    pose.sideShare = 0.3;  // resting on a side, the intermediate axis up
    pose.endShare = 0.1;   // standing on an end, the long axis up
    pose.settle = false;   // models keep these rests rather than tipping over
    const BoulderList list = BoulderLayer(options).Boulders({-20.0, -20.0, 20.0, 20.0});
    // --8<-- [end:pose_boulders]
    ASSERT_GT(list.Size(), 0u);
    for (const double burial : list.Burials()) {
        EXPECT_GE(burial, 0.2 - 1e-9);
        EXPECT_LE(burial, 0.5 + 1e-9);
    }
}

}  // namespace
}  // namespace terraingen
