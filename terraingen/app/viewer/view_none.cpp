#include "terraingen/app/cli.h"
#include "terraingen/app/viewer/view.h"

namespace terraingen::cli {

bool ViewerAvailable() {
    return false;
}

int RunView(const std::vector<std::string>& /*args*/, std::ostream& /*out*/, std::ostream& err) {
    err << "terraingen: this build has no viewer; configure TerrainGen with "
           "-DTERRAINGEN_BUILD_VIEWER=ON -DTERRAINGEN_ERNIE_DIR=<path to an Ernie checkout>\n";
    return kExitUsage;
}

}  // namespace terraingen::cli
