#include "terraingen/io/geotiff.h"

#include <cstdio>
#include <exception>
#include <string>

/// Reads band 1 of a GeoTIFF with TerrainGen and writes it back with TerrainGen, so that
/// `check_geotiff_with_gdal.py` can compare GDAL's view of the input and the output.
///
/// Usage: terraingen_geotiff_roundtrip INPUT OUTPUT [lzw|deflate|none]
int main(int argc, char** argv) {
    if (argc < 3 || argc > 4) {
        std::fputs("Usage: terraingen_geotiff_roundtrip INPUT OUTPUT [lzw|deflate|none]\n", stderr);
        return 2;
    }
    try {
        const terraingen::GeoTiff tiff = terraingen::ReadGeoTiff(argv[1]);
        terraingen::GeoTiffWriteOptions options;
        const std::string compression = argc == 4 ? argv[3] : "lzw";
        if (compression == "none") {
            options.compression = terraingen::TiffCompression::None;
        } else if (compression == "deflate") {
            options.compression = terraingen::TiffCompression::Deflate;
        } else if (compression != "lzw") {
            std::fprintf(stderr, "Unknown compression %s\n", compression.c_str());
            return 2;
        }
        terraingen::WriteGeoTiff(argv[2], tiff.raster, tiff.geoKeys, options);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
    return 0;
}
