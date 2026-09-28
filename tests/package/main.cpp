#include "terraingen/build_info.h"
#include "terraingen/version.h"

#include <cstdio>

int main() {
    const terraingen::Version version = terraingen::GetVersion();
    std::printf("terraingen %d.%d.%d (%s)\n", version.major, version.minor, version.patch,
                terraingen::GetBuildInfo().gitSha.c_str());

    const bool headersMatchLibrary = version.major == TERRAINGEN_VERSION_MAJOR &&
                                     version.minor == TERRAINGEN_VERSION_MINOR &&
                                     version.patch == TERRAINGEN_VERSION_PATCH;
    return headersMatchLibrary ? 0 : 1;
}
