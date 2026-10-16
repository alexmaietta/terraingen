#include "terraingen/layers/craters/crater.h"

#include <gtest/gtest.h>

#include <stdexcept>
#include <string>

using terraingen::Crater;
using terraingen::CraterSelection;

TEST(CraterSelection, IncludesCratersWithinItsRanges) {
    const Crater crater{0.0, 0.0, 100.0, 1.0, 7u};
    EXPECT_TRUE(CraterSelection{}.Includes(crater));
    EXPECT_FALSE(CraterSelection{.minDiameter = 200.0}.Includes(crater));
    EXPECT_FALSE(CraterSelection{.maxDiameter = 50.0}.Includes(crater));
    const CraterSelection exact{.minDiameter = 100.0, .maxDiameter = 100.0};
    EXPECT_TRUE(exact.Includes(crater));
    EXPECT_FALSE(CraterSelection{.minAgeGyr = 2.0}.Includes(crater));
    EXPECT_FALSE(CraterSelection{.maxAgeGyr = 0.5}.Includes(crater));
    Crater manual = crater;
    manual.manual = true;
    EXPECT_TRUE(CraterSelection{}.Includes(manual));
    EXPECT_FALSE(CraterSelection{.manual = false}.Includes(manual));
    EXPECT_TRUE(CraterSelection{.manual = false}.Includes(crater));
}

TEST(ValidateCraterSelection, RejectsInvertedOrNegativeRangesNamingTheModel) {
    EXPECT_NO_THROW(terraingen::ValidateCraterSelection({}, "Test"));
    for (const CraterSelection& invalid : {CraterSelection{.minDiameter = -1.0},
                                           CraterSelection{.minDiameter = 2.0, .maxDiameter = 1.0},
                                           CraterSelection{.minAgeGyr = -0.1},
                                           CraterSelection{.minAgeGyr = 2.0, .maxAgeGyr = 1.0}}) {
        try {
            terraingen::ValidateCraterSelection(invalid, "Test");
            ADD_FAILURE() << "accepted an invalid selection";
        } catch (const std::invalid_argument& error) {
            EXPECT_EQ(std::string(error.what()),
                      "Test size and age ranges must satisfy 0 <= min <= max");
        }
    }
}
