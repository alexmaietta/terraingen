// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief The fixture docs snippets share, through the public API only.
#pragma once

#include <filesystem>

#include <gtest/gtest.h>

namespace terraingen {

/// A docs snippet.
class SnippetTest : public testing::Test {
  protected:
    /// A fresh folder for files a snippet writes.
    static std::filesystem::path OutputFolder() {
        const std::filesystem::path folder =
            std::filesystem::path(testing::TempDir()) / "terraingen_snippets";
        std::filesystem::create_directories(folder);
        return folder;
    }

    /// `OutputFolder` made the working directory, for snippets that write files by bare name.
    static std::filesystem::path InOutputFolder() {
        const std::filesystem::path folder = OutputFolder();
        std::filesystem::current_path(folder);
        return folder;
    }
};

}  // namespace terraingen
