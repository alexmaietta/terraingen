#include "terraingen/models/size_frequency.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using terraingen::HartmannEquilibrium;
using terraingen::LogPolynomial;
using terraingen::MooreDistribution;
using terraingen::MooreTerrain;
using terraingen::NeukumLunarChronology;
using terraingen::NeukumProductionFunction;
using terraingen::PiecewisePowerLaw;
using terraingen::PowerLaw;
using terraingen::PowerLawCoefficientFromKilometres;
using terraingen::ProductionSurface;
using terraingen::TabulatedSfd;
using terraingen::TraskEquilibrium;
using terraingen::ViperSpecification;

TEST(Presets, TraskAndHartmannMatchTheirPublishedForms) {
    EXPECT_DOUBLE_EQ(TraskEquilibrium().Cumulative(1.0), std::pow(10.0, -1.1));
    EXPECT_DOUBLE_EQ(TraskEquilibrium().Cumulative(100.0), std::pow(10.0, -1.1) * 1e-4);
    EXPECT_NEAR(HartmannEquilibrium().Cumulative(1000.0), std::pow(10.0, -1.14) * 1e-6, 1e-20);
    EXPECT_DOUBLE_EQ(PowerLawCoefficientFromKilometres(std::pow(10.0, -1.1), 2.0),
                     std::pow(10.0, -1.1));
}

TEST(Presets, MooreTerrainsAreContinuousAndMatchTheirSegments) {
    EXPECT_DOUBLE_EQ(MooreDistribution(MooreTerrain::RoughMare).Cumulative(1000.0), 10.0 * 1e-9);
    EXPECT_DOUBLE_EQ(MooreDistribution(MooreTerrain::Uplands).Cumulative(10.0), 0.1 * 1e-2);
    EXPECT_DOUBLE_EQ(MooreDistribution(MooreTerrain::Uplands).Cumulative(1e4), 100.0 * 1e-12);
    const PiecewisePowerLaw smooth = MooreDistribution(MooreTerrain::SmoothMare);
    EXPECT_EQ(smooth.Segments().size(), 4u);
    EXPECT_NEAR(smooth.Cumulative(150.0), std::pow(10.0, -2.038) * std::pow(150.0, -1.68), 1e-18);
    const PiecewisePowerLaw heavy = MooreDistribution(MooreTerrain::HeavilyCratered);
    EXPECT_EQ(heavy.ValidRange().max, 10000.0);
    EXPECT_THROW(heavy.Cumulative(20000.0), std::domain_error);
}

TEST(Presets, ViperSpecificationDropsByAFactorOf146At80Metres) {
    const PiecewisePowerLaw viper = ViperSpecification();
    const double below = viper.Cumulative(80.0);
    const double above = viper.Cumulative(80.0 * (1.0 + 1e-12));
    EXPECT_NEAR(below / above, 1.458, 1e-3);
    EXPECT_DOUBLE_EQ(viper.Cumulative(1.0), 29174.0 / 1e6);
}

TEST(Presets, MatchSynthterrainReferenceValues) {
    std::ifstream file(TERRAINGEN_VALIDATION_REFERENCES "/sfd/synthterrain_reference.csv");
    ASSERT_TRUE(file.is_open());
    const PowerLaw trask = TraskEquilibrium();
    const PiecewisePowerLaw viper = ViperSpecification();
    const LogPolynomial npf = NeukumProductionFunction();
    std::string line;
    int compared = 0;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#' || line.rfind("name", 0) == 0) {
            continue;
        }
        std::stringstream fields(line);
        std::string name;
        std::string diameter;
        std::string value;
        std::getline(fields, name, ',');
        std::getline(fields, diameter, ',');
        std::getline(fields, value, ',');
        const double d = std::stod(diameter);
        const double expected = std::stod(value);
        const double actual = name == "trask"   ? trask.Cumulative(d)
                              : name == "viper" ? viper.Cumulative(d)
                                                : npf.Cumulative(d);
        EXPECT_NEAR(actual, expected, 1e-12 * expected) << name << " at " << d << " m";
        ++compared;
    }
    EXPECT_EQ(compared, 68);
}

TEST(Presets, NeukumPresentDayRateMatchesTheProductionFunctionAtOneKilometre) {
    const double rate = NeukumProductionFunction().Cumulative(1000.0);
    EXPECT_NEAR(rate * 1e6, 8.38e-4, 1e-9);
    const double step = 1e-6;
    EXPECT_NEAR(NeukumLunarChronology(step) / step, rate, 1e-6 * rate);
}

namespace {

std::pair<std::vector<double>, std::vector<double>> ReadDsneTable() {
    std::ifstream file(TERRAINGEN_VALIDATION_REFERENCES "/sfd/dsne_rev_i_table_3_4_1_1_1.csv");
    if (!file.is_open()) {
        throw std::runtime_error("Missing DSNE reference table");
    }
    std::vector<double> diameters;
    std::vector<double> values;
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#' || line.rfind("diameter", 0) == 0) {
            continue;
        }
        const std::size_t comma = line.find(',');
        diameters.push_back(std::stod(line.substr(0, comma)));
        values.push_back(std::stod(line.substr(comma + 1)));
    }
    return {diameters, values};
}

}  // namespace

TEST(Presets, TabulatedDsneTableFollowsTheSurfaceModelBetweenRows) {
    auto [diameters, values] = ReadDsneTable();
    ASSERT_EQ(diameters.size(), 30u);
    const TabulatedSfd table(diameters, values, terraingen::SfdKind::Density, 1000.0, 1e6);
    const ProductionSurface surface(std::make_shared<LogPolynomial>(NeukumProductionFunction()),
                                    3.5);
    const PowerLaw trask = TraskEquilibrium();
    int compared = 0;
    for (std::size_t i = 0; i + 1 < diameters.size(); ++i) {
        const double low = diameters[i] * 1000.0;
        const double high = diameters[i + 1] * 1000.0;
        EXPECT_NEAR(table.Cumulative(low), values[i] / 1e6, 1e-12 * values[i]);
        const bool lowIsProduction = surface.Cumulative(low) < trask.Cumulative(low);
        const bool highIsProduction = surface.Cumulative(high) < trask.Cumulative(high);
        if (lowIsProduction != highIsProduction) {
            continue;
        }
        const double middle = std::sqrt(low * high);
        const double model = std::min(surface.Cumulative(middle), trask.Cumulative(middle));
        EXPECT_NEAR(table.Cumulative(middle), model, 0.01 * model) << "D = " << middle << " m";
        ++compared;
    }
    EXPECT_EQ(compared, 28);
}

TEST(Presets, NeukumSurfaceAndTraskEquilibriumReproduceTheDsneMareTable) {
    std::ifstream file(TERRAINGEN_VALIDATION_REFERENCES "/sfd/dsne_rev_i_table_3_4_1_1_1.csv");
    ASSERT_TRUE(file.is_open());
    const ProductionSurface surface(std::make_shared<LogPolynomial>(NeukumProductionFunction()),
                                    3.5);
    const PowerLaw trask = TraskEquilibrium();
    std::string line;
    int compared = 0;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#' || line.rfind("diameter", 0) == 0) {
            continue;
        }
        std::stringstream fields(line);
        std::string diameter;
        std::string value;
        std::getline(fields, diameter, ',');
        std::getline(fields, value, ',');
        const double d = std::stod(diameter) * 1000.0;
        const double expected = std::stod(value) / 1e6;
        const double actual = std::min(surface.Cumulative(d), trask.Cumulative(d));
        EXPECT_NEAR(actual, expected, 0.01 * expected) << "D = " << d << " m";
        ++compared;
    }
    EXPECT_EQ(compared, 30);
}
