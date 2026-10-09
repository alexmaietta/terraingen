#pragma once

#include <filesystem>
#include <ostream>
#include <string>
#include <vector>

namespace terraingen::cli {

/// Whether this build has the viewer (`TERRAINGEN_BUILD_VIEWER`).
bool ViewerAvailable();

/// Runs `terraingen view [PATH] [--threads N]`, with `args` the arguments after `view`: opens the
/// viewer on the terrain configuration (.json) or heights (.tif, .tiff) at PATH, or empty, and
/// returns once its window closes. Without the viewer in the build it explains how to add it.
/// @return `kExitSuccess`, `kExitInvalidConfig` if the viewer cannot start, or `kExitUsage` for
/// bad arguments or a build without the viewer.
int RunView(const std::vector<std::string>& args, std::ostream& out, std::ostream& err);

}  // namespace terraingen::cli
