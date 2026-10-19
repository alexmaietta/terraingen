#include "terraingen/layers/craters/crater.h"

#include <gtest/gtest.h>

#include <limits>
#include <stdexcept>
#include <string>

using terraingen::Crater;
using terraingen::CraterSelection;

namespace {

/// A selection of every diameter, aged from `minAgeGyr` to `maxAgeGyr`.
CraterSelection Ages(double minAgeGyr, double maxAgeGyr = std::numeric_limits<double>::infinity()) {
    CraterSelection selection;
    selection.minAgeGyr = minAgeGyr;
    selection.maxAgeGyr = maxAgeGyr;
    return selection;
}

}  // namespace

TEST(CraterSelection, IncludesCratersWithinItsRanges) {
    const Crater crater{0.0, 0.0, 100.0, 1.0, 7u};
    EXPECT_TRUE(CraterSelection{}.Includes(crater));
    EXPECT_FALSE(CraterSelection{200.0}.Includes(crater));
    EXPECT_FALSE((CraterSelection{0.0, 50.0}).Includes(crater));
    const CraterSelection exact{100.0, 100.0};
    EXPECT_TRUE(exact.Includes(crater));
    EXPECT_FALSE(Ages(2.0).Includes(crater));
    EXPECT_FALSE(Ages(0.0, 0.5).Includes(crater));
    Crater manual = crater;
    manual.manual = true;
    EXPECT_TRUE(CraterSelection{}.Includes(manual));
    CraterSelection generated;
    generated.manual = false;
    EXPECT_FALSE(generated.Includes(manual));
    EXPECT_TRUE(generated.Includes(crater));
}

TEST(ValidateCraterSelection, RejectsInvertedOrNegativeRangesNamingTheModel) {
    EXPECT_NO_THROW(terraingen::ValidateCraterSelection({}, "Test"));
    for (const CraterSelection& invalid :
         {CraterSelection{-1.0}, CraterSelection{2.0, 1.0}, Ages(-0.1), Ages(2.0, 1.0)}) {
        try {
            terraingen::ValidateCraterSelection(invalid, "Test");
            ADD_FAILURE() << "accepted an invalid selection";
        } catch (const std::invalid_argument& error) {
            EXPECT_EQ(std::string(error.what()),
                      "Test size and age ranges must satisfy 0 <= min <= max");
        }
    }
}
