// SPDX-License-Identifier: BSD-3-Clause

#include "util/text.h"

#include <gtest/gtest.h>

#include <cmath>

TEST(Text, LowersAsciiLettersOnly) {
    EXPECT_EQ(terraingen::Lower("GeoTIFF_1.TIF"), "geotiff_1.tif");
    EXPECT_EQ(terraingen::Lower(""), "");
}

TEST(Text, MatchesExtensionsIgnoringCase) {
    EXPECT_TRUE(terraingen::HasExtension("dem.TIF", {".tif", ".tiff"}));
    EXPECT_TRUE(terraingen::HasExtension("dir/dem.tiff", {".tif", ".TIFF"}));
    EXPECT_FALSE(terraingen::HasExtension("dem.tif.json", {".tif"}));
    EXPECT_FALSE(terraingen::HasExtension("tif", {".tif"}));
    EXPECT_FALSE(terraingen::HasExtension("dem.tif", {}));
}

TEST(Text, FormatsNumbersToTheGivenDigits) {
    EXPECT_EQ(terraingen::FormatNumber(1.0 / 3.0, 9), "0.333333333");
    EXPECT_EQ(terraingen::FormatNumber(1.0 / 3.0, 17), "0.33333333333333331");
    EXPECT_EQ(terraingen::FormatNumber(0.1, 9), "0.1");
    EXPECT_EQ(terraingen::FormatNumber(-9999.0, 9), "-9999");
    EXPECT_EQ(terraingen::FormatNumber(1.5e-12, 9), "1.5e-12");
}

TEST(Text, GivesTheShortestDegreesThatTurnBackIntoTheRadians) {
    using terraingen::DegreesFromRadians;
    const double degree = terraingen::kPi / 180.0;
    EXPECT_EQ(DegreesFromRadians(29.0 * degree), 29.0);
    EXPECT_EQ(DegreesFromRadians(0.0), 0.0);
    EXPECT_EQ(DegreesFromRadians(-90.0 * degree), -90.0);
    for (const double radians : {0.5, 0.35, 0.03, 0.8, 4.3, 0.26, 1e-9, 123.456}) {
        EXPECT_EQ(terraingen::RadiansFromDegrees(DegreesFromRadians(radians)), radians) << radians;
    }
    EXPECT_TRUE(std::isnan(DegreesFromRadians(std::nan(""))));
    EXPECT_TRUE(std::isinf(DegreesFromRadians(HUGE_VAL)));
}
