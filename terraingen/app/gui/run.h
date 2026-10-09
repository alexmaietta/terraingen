#pragma once

#include <ostream>
#include <string>
#include <vector>

namespace terraingen::cli {

/// Whether this build has the GUI (`TERRAINGEN_BUILD_GUI`).
bool GuiAvailable();

/// Runs `terraingen [PATH] --gui [--threads N]`, with `args` the arguments but `--gui`: opens the
/// GUI on the terrain configuration (.json) or heights (.tif, .tiff) at PATH, or none, and returns
/// once its window closes. Without the GUI in the build it explains how to add it.
/// @return `kExitSuccess`, `kExitInvalidConfig` if the GUI cannot start, or `kExitUsage` for bad
/// arguments or a build without the GUI.
/// @throws std::invalid_argument for bad arguments, as `Run` reports them.
int RunGui(const std::vector<std::string>& args, std::ostream& out, std::ostream& err);

}  // namespace terraingen::cli
