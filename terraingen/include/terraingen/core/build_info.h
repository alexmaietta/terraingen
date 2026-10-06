#pragma once

#include <string>

namespace terraingen {

/// Build metadata captured when the library was compiled.
///
/// Regenerated whenever the library is rebuilt or the git state changes (see
/// cmake/BuildInfo.cmake), so it describes the linked library binary,
/// not the headers a caller compiled against. Builds without changes keep it,
/// and relink nothing.
struct BuildInfo {
    std::string gitSha;  ///< Short commit SHA, or "unknown" outside a git checkout.
    bool gitDirty;       ///< True if built from a working tree with uncommitted changes.
    /// When the library was last rebuilt, as ISO-8601 UTC, e.g. "2026-08-12T19:40:00Z", or
    /// SOURCE_DATE_EPOCH when set.
    std::string buildDate;
};

/// Returns build metadata for the linked library.
BuildInfo GetBuildInfo();

}  // namespace terraingen
