// SPDX-License-Identifier: BSD-3-Clause

#include "util/text.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <stdexcept>
#include <string>

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
