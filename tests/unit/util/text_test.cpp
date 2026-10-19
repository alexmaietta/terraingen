#include "terraingen/util/arithmetic.h"
#include "terraingen/util/files.h"
#include "terraingen/util/text.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>

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

TEST(Arithmetic, CeilDivRoundsUp) {
    EXPECT_EQ(terraingen::CeilDiv<std::size_t>(0, 4), 0u);
    EXPECT_EQ(terraingen::CeilDiv<std::size_t>(1, 4), 1u);
    EXPECT_EQ(terraingen::CeilDiv<std::size_t>(8, 4), 2u);
    EXPECT_EQ(terraingen::CeilDiv<std::size_t>(9, 4), 3u);
    static_assert(terraingen::CeilDiv<std::uint32_t>(257, 256) == 2u);
}

TEST(Files, WritesAndReadsWholeFiles) {
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "terraingen_files_test.bin";
    const std::string bytes("a\0b\nc", 5);
    terraingen::WriteFile(path, bytes);
    EXPECT_EQ(terraingen::ReadFile(path), bytes);
    terraingen::WriteFile(path, "");
    EXPECT_EQ(terraingen::ReadFile(path), "");
    std::filesystem::remove(path);
}

TEST(Files, NamesThePathItCannotOpen) {
    const std::filesystem::path missing =
        std::filesystem::temp_directory_path() / "terraingen_no_such_dir" / "missing.txt";
    try {
        terraingen::ReadFile(missing);
        FAIL() << "ReadFile did not throw";
    } catch (const std::runtime_error& error) {
        EXPECT_EQ(std::string(error.what()), "Cannot read " + missing.string());
    }
    EXPECT_THROW(terraingen::WriteFile(missing, "x"), std::runtime_error);
}
