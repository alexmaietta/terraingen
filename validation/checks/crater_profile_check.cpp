#include "terraingen/craters.h"

#include <gtest/gtest.h>

#include <cmath>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

using terraingen::CraterMorphometry;
using terraingen::CraterState;
using terraingen::FassettThomsonProfile;
using terraingen::FassettThomsonRim;
using terraingen::kFreshRimHeightRatio;
using terraingen::MartinParkesDunstanProfile;

TEST(CraterProfiles, MatchSynthterrainReferenceValues) {
    std::ifstream file(TERRAINGEN_VALIDATION_REFERENCES "/craters/synthterrain_profiles.csv");
    ASSERT_TRUE(file.is_open());
    const FassettThomsonProfile original(FassettThomsonRim::Original);
    const FassettThomsonProfile atOne(FassettThomsonRim::AtOneRadius);
    const FassettThomsonProfile plateau(FassettThomsonRim::Plateau);
    const MartinParkesDunstanProfile mpd;
    std::map<std::string, int> compared;
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#' || line.rfind("name", 0) == 0) {
            continue;
        }
        std::stringstream fields(line);
        std::vector<std::string> values;
        for (std::string value; std::getline(fields, value, ',');) {
            values.push_back(value);
        }
        ASSERT_EQ(values.size(), 6u) << line;
        const std::string& name = values[0];
        const double diameter = std::stod(values[1]);
        const double depth = std::stod(values[2]);
        const double rim = std::stod(values[3]);
        const double distance = std::stod(values[4]) * 0.5 * diameter;
        const double expected = std::stod(values[5]);
        double actual = 0.0;
        if (name == "ft_original" || name == "ft_at_one_radius") {
            const CraterMorphometry shape{kFreshRimHeightRatio + 0.181, kFreshRimHeightRatio};
            const FassettThomsonProfile& profile = name == "ft_original" ? original : atOne;
            actual = profile.Height(distance, diameter, {shape, shape});
        } else if (name == "ft_plateau") {
            const CraterMorphometry shape{depth / diameter, kFreshRimHeightRatio};
            actual = plateau.Height(distance, diameter, {shape, shape});
        } else {
            const double freshRim = 0.036 * std::pow(diameter, 1.01) / diameter;
            const CraterState state{{0.196 * std::pow(diameter, 0.01), freshRim},
                                    {depth / diameter, rim / diameter}};
            actual = mpd.Height(distance, diameter, state);
        }
        EXPECT_NEAR(actual, expected, 1e-9 * diameter) << line;
        ++compared[name];
    }
    EXPECT_EQ(compared["ft_original"], 33);
    EXPECT_EQ(compared["ft_at_one_radius"], 33);
    EXPECT_EQ(compared["ft_plateau"], 66);
    EXPECT_EQ(compared["mpd"], 63);
}
