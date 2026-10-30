// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/analysis/raster_stats.h"
#include "terraingen/sources/synthetic_source.h"
#include "terraingen/util/build_info.h"
#include "terraingen/util/version.h"

#include <cmath>
#include <cstdio>

int main() {
    const terraingen::Version version = terraingen::GetVersion();
    std::printf("terraingen %d.%d.%d (%s)\n", version.major, version.minor, version.patch,
                terraingen::GetBuildInfo().gitSha.c_str());

    const bool headersMatchLibrary = version.major == TERRAINGEN_VERSION_MAJOR &&
                                     version.minor == TERRAINGEN_VERSION_MINOR &&
                                     version.patch == TERRAINGEN_VERSION_PATCH;

    std::vector<terraingen::Fractal> params{terraingen::Fractal{}};
    params[0].amplitude = 10.0f;
    terraingen::Raster<float> terrain =
        terraingen::RasterGrid{32, 32, terraingen::GeoTransform::NorthUp(-16.0, 16.0, 1.0)}
            .MakeRaster();
    terraingen::SyntheticSource(params).Fill(terrain);
    const terraingen::RasterStats stats = terraingen::ComputeStats(terrain);
    std::printf("synthetic terrain: %zu cells, %.3f to %.3f m\n", stats.validCount, stats.min,
                stats.max);
    const bool terrainOk = stats.validCount == 32 * 32 && std::fabs(stats.max) <= 10.0 &&
                           std::fabs(stats.min) <= 10.0 && stats.max > stats.min;

    return headersMatchLibrary && terrainOk ? 0 : 1;
}
