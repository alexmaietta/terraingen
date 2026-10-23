// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief Build metadata of the linked library.
#pragma once

#include <string>

namespace terraingen {

/// Build metadata captured when the linked library was last rebuilt (see
/// cmake/BuildInfo.cmake); it describes the linked libraries, not the headers compiled against.
struct BuildInfo {
    std::string gitSha;  ///< Short commit SHA, or "unknown" outside a git checkout.
    bool gitDirty;       ///< True if built from a working tree with uncommitted changes.
    /// When the library was last rebuilt, as ISO-8601 UTC, e.g. "2026-08-12T19:40:00Z", or
    /// SOURCE_DATE_EPOCH when set.
    std::string buildDate;
};

/// The commit, dirty flag and build date of the linked library (terraingen::build_info).
BuildInfo GetBuildInfo();

}  // namespace terraingen
