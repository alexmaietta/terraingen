// SPDX-License-Identifier: BSD-3-Clause

#include "app/cli.h"
#include "app/gui/run.h"

namespace terraingen::cli {

bool GuiAvailable() {
    return false;
}

int RunGui(const std::vector<std::string>& args, std::ostream& /*out*/, std::ostream& err) {
    ParseGuiArgs(args);
    err << "terraingen: this build has no GUI; configure TerrainGen with "
           "-DTERRAINGEN_ERNIE_DIR=<path to an Ernie checkout>\n";
    return kExitUsage;
}

}  // namespace terraingen::cli
