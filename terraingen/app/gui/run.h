/// @file
/// @brief The `terraingen --gui` command, with or without the GUI in the build.
#pragma once

#include <ostream>
#include <string>
#include <vector>

namespace terraingen::cli {

/// Whether this build has the GUI (`TERRAINGEN_BUILD_GUI`).
bool GuiAvailable();

/// Runs `terraingen [PATH] --gui [--threads N] [--backend NAME]` (`ParseGuiArgs`): opens the GUI on
/// a configuration (.json), heights (.tif, .tiff) or the starter terrain, generating on the
/// backend NAME, and returns once its window closes. Without the GUI in the build it checks the
/// arguments and explains how to add it.
/// @param args The arguments, without the program name and `--gui`.
/// @return `kExitSuccess`, `kExitFailure` if the GUI cannot start or stops on an error, or
/// `kExitUsage` for a build without the GUI.
/// @throws std::invalid_argument for bad arguments, which `Run` reports as usage errors.
int RunGui(const std::vector<std::string>& args, std::ostream& out, std::ostream& err);

}  // namespace terraingen::cli
