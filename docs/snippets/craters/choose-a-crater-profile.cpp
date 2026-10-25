// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief Choose a Crater Profile
///
/// A crater profile gives a crater's height by distance from its center for its depth and rim.
/// Martin, Parkes and Dunstan's polynomials give a round bowl, while Fassett and Thomson's fits
/// give straight walls and a flat floor, with a choice of rims. Any of them is the last of a
/// layer's models, and each can be read directly for a crater state.
#include "snippet_test.h"
#include "terraingen/terraingen.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace terraingen {
namespace {

TEST_F(SnippetTest, ChooseACraterProfile) {
    const double diameter = 100.0;
    // --8<-- [start:choose_a_crater_profile]
    const std::map<std::string, std::shared_ptr<const CraterProfile>> profiles{
        {"Martin, Parkes and Dunstan", std::make_shared<MartinParkesDunstanProfile>()},
        {"Fassett and Thomson", std::make_shared<FassettThomsonProfile>()},
        {"Fassett and Thomson, plateau",
         std::make_shared<FassettThomsonProfile>(FassettThomsonRim::Plateau)},
    };
    const CraterMorphometry fresh = LunarFreshCraterMorphometry(diameter);
    const CraterState state{fresh, fresh};  // fresh and current shape alike
    std::map<std::string, std::vector<double>> heights;
    for (const auto& [name, profile] : profiles) {
        for (int step = 0; step <= 200; ++step) {
            heights[name].push_back(profile->Height(0.01 * step * 0.5 * diameter, diameter, state));
        }
    }
    // --8<-- [end:choose_a_crater_profile]
    for (const auto& [name, curve] : heights) {
        EXPECT_LT(curve.front(), 0.0) << name;
        EXPECT_GT(curve[100], 0.0) << name;
    }
}

}  // namespace
}  // namespace terraingen
