// SPDX-License-Identifier: BSD-3-Clause

#include "util/text.h"

#include <gtest/gtest.h>

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
