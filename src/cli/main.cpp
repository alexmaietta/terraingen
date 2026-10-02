#include "terraingen/build_info.h"
#include "terraingen/version.h"

#include <cstdio>

int main() {
    terraingen::Version version = terraingen::GetVersion();
    terraingen::BuildInfo buildInfo = terraingen::GetBuildInfo();

    std::printf("TerrainGen %d.%d.%d\n", version.major, version.minor, version.patch);
    std::printf("  git commit:  %s%s\n", buildInfo.gitSha.c_str(),
                buildInfo.gitDirty ? "-dirty" : "");
    std::printf("  build date:  %s\n", buildInfo.buildDate.c_str());
    std::printf("  api version: %d\n", terraingen::GetApiVersion());
    return 0;
}
