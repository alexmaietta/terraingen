#include "terraingen/io/geotiff.h"

#include <cstdio>
#include <exception>
#include <string>

/// Reads band 1 of a GeoTIFF with TerrainGen and writes it back with TerrainGen, so that
/// `check_geotiff_with_gdal.py` can compare GDAL's view of the input and the output.
///
/// Usage: terraingen_geotiff_roundtrip INPUT OUTPUT [lzw|none]
int main(int argc, char** argv) {
    if (argc < 3 || argc > 4) {
        std::fputs("Usage: terraingen_geotiff_roundtrip INPUT OUTPUT [lzw|none]\n", stderr);
        return 2;
    }
    try {
        const terraingen::GeoTiff tiff = terraingen::ReadGeoTiff(argv[1]);
        terraingen::GeoTiffWriteOptions options;
        if (argc == 4 && std::string(argv[3]) == "none") {
            options.compression = terraingen::TiffCompression::None;
        }
        terraingen::WriteGeoTiff(argv[2], tiff.raster, tiff.geoKeys, options);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
    return 0;
}
