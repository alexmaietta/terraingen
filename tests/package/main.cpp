#include "terraingen/build_info.h"
#include "terraingen/core/raster_stats.h"
#include "terraingen/models/synthetic_terrain.h"
#include "terraingen/version.h"

#include <cmath>
#include <cstdio>

int main() {
    const terraingen::Version version = terraingen::GetVersion();
    std::printf("terraingen %d.%d.%d (%s)\n", version.major, version.minor, version.patch,
                terraingen::GetBuildInfo().gitSha.c_str());

    const bool headersMatchLibrary = version.major == TERRAINGEN_VERSION_MAJOR &&
                                     version.minor == TERRAINGEN_VERSION_MINOR &&
                                     version.patch == TERRAINGEN_VERSION_PATCH;

    terraingen::SyntheticTerrainParams params;
    params.layers[0].fractal.amplitude = 10.0f;
    const auto terrain = terraingen::GenerateSyntheticTerrain(32, 32, 1.0, params);
    const terraingen::RasterStats stats = terraingen::ComputeStats(terrain);
    std::printf("synthetic terrain: %zu cells, %.3f to %.3f m\n", stats.validCount, stats.min,
                stats.max);
    const bool terrainOk = stats.validCount == 32 * 32 && std::fabs(stats.max) <= 10.0 &&
                           std::fabs(stats.min) <= 10.0 && stats.max > stats.min;

    return headersMatchLibrary && terrainOk ? 0 : 1;
}
