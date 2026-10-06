#pragma once

#include <string>

namespace terraingen {

/// Build metadata captured when the library was compiled.
///
/// Regenerated on every build (see cmake/GenerateBuildInfo.cmake), so it
/// describes the linked library binary, not the headers a caller compiled
/// against. The generated source only changes with the commit, the dirty state
/// or the day, so builds without changes relink nothing.
struct BuildInfo {
    std::string gitSha;  ///< Short commit SHA, or "unknown" outside a git checkout.
    bool gitDirty;       ///< True if built from a working tree with uncommitted changes.
    /// Build day as ISO-8601 UTC, e.g. "2026-08-12", or the day of SOURCE_DATE_EPOCH when set.
    std::string buildDate;
};

/// Returns build metadata for the linked library.
BuildInfo GetBuildInfo();

}  // namespace terraingen
