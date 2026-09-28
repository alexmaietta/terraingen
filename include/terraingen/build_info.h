#pragma once

#include <string>

namespace terraingen {

/// Build metadata captured when the library was compiled.
///
/// Regenerated on every build (see cmake/GenerateBuildInfo.cmake), so it
/// describes the linked library binary, not the headers a caller compiled
/// against.
struct BuildInfo {
    std::string gitSha;     ///< Short commit SHA, or "unknown" outside a git checkout.
    bool gitDirty;          ///< True if built from a working tree with uncommitted changes.
    std::string buildDate;  ///< Build time as ISO-8601 UTC, e.g. "2026-08-12T19:40:00Z".
};

/// Returns build metadata for the linked library.
BuildInfo GetBuildInfo();

}  // namespace terraingen
