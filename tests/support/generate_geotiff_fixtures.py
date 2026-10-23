# SPDX-License-Identifier: BSD-3-Clause

"""Writes small GeoTIFFs with GDAL in every layout TerrainGen reads and stores their bytes, with the
values, transform and no-data GDAL reads back, in geotiff_fixtures.inc for hermetic tests.

Run from the repository root with the validation environment (rasterio bundles GDAL):
    .venv/bin/python tests/support/generate_geotiff_fixtures.py
"""

import math
import tempfile
from pathlib import Path

import numpy as np
import rasterio
from rasterio.crs import CRS
from rasterio.transform import Affine

OUTPUT = Path(__file__).with_name("geotiff_fixtures.inc")
WIDTH, HEIGHT = 21, 19
MOON = CRS.from_proj4("+proj=eqc +R=1737400 +units=m +no_defs")
NORTH_UP = Affine(2.5, 0.0, -1200.0, 0.0, -2.5, 3400.0)
ROTATED = Affine(2.0, 0.5, 100.0, 0.25, -2.0, 50.0)


def pattern(dtype, bands=1):
    y, x = np.mgrid[0:HEIGHT, 0:WIDTH]
    data = []
    for band in range(bands):
        values = x * 3.5 - y * 2.25 + 40.0 * np.sin(x * 0.4 + band) + band * 7
        if np.issubdtype(dtype, np.floating):
            data.append((values * 1.37).astype(dtype))
        elif np.issubdtype(dtype, np.unsignedinteger):
            data.append(np.round(values + 80).astype(dtype))
        else:
            data.append(np.round(values).astype(dtype))
    return np.stack(data)


FIXTURES = [
    ("float32_none_strips", np.float32, 1, dict(compress=None, rowsperstrip=7)),
    (
        "float32_lzw_fp_tiles",
        np.float32,
        1,
        dict(compress="lzw", predictor=3, tiled=True, blockxsize=16, blockysize=16),
    ),
    ("float32_lzw_horizontal_strips", np.float32, 1, dict(compress="lzw", predictor=2)),
    (
        "float32_deflate_fp_tiles",
        np.float32,
        1,
        dict(compress="deflate", predictor=3, tiled=True, blockxsize=16, blockysize=16),
    ),
    ("int16_deflate_horizontal_strips", np.int16, 1, dict(compress="deflate", predictor=2)),
    ("float64_deflate_big_endian", np.float64, 1, dict(compress="deflate", ENDIANNESS="BIG")),
    (
        "float64_lzw_fp_tiles",
        np.float64,
        1,
        dict(compress="lzw", predictor=3, tiled=True, blockxsize=16, blockysize=16),
    ),
    ("int16_lzw_horizontal_nodata", np.int16, 1, dict(compress="lzw", predictor=2, nodata=-32768)),
    ("uint8_packbits_strips", np.uint8, 1, dict(compress="packbits", rowsperstrip=5)),
    ("int8_none_strips", np.int8, 1, dict(compress=None)),
    ("uint16_lzw_strips", np.uint16, 1, dict(compress="lzw")),
    ("int32_lzw_horizontal_big_endian", np.int32, 1, dict(compress="lzw", predictor=2, ENDIANNESS="BIG")),
    ("uint32_none_tiles", np.uint32, 1, dict(tiled=True, blockxsize=16, blockysize=16)),
    ("int64_lzw_strips", np.int64, 1, dict(compress="lzw")),
    (
        "float64_none_tiles_big_endian",
        np.float64,
        1,
        dict(tiled=True, blockxsize=16, blockysize=16, ENDIANNESS="BIG"),
    ),
    ("float32_lzw_fp_big_endian", np.float32, 1, dict(compress="lzw", predictor=3, ENDIANNESS="BIG")),
    ("float32_lzw_bigtiff", np.float32, 1, dict(compress="lzw", predictor=3, BIGTIFF="YES")),
    ("uint16_three_band_pixel", np.uint16, 3, dict(compress="lzw", predictor=2, interleave="pixel")),
    ("uint16_three_band_planar", np.uint16, 3, dict(compress="lzw", predictor=2, interleave="band")),
    ("float32_pixel_is_point", np.float32, 1, dict(compress="lzw", area_or_point="Point")),
    ("float32_rotated", np.float32, 1, dict(compress="lzw", transform=ROTATED)),
    ("float32_no_georeferencing", np.float32, 1, dict(crs=None, transform=None)),
    (
        "int16_scaled_nodata",
        np.int16,
        1,
        dict(compress="lzw", nodata=-32768, scales=(0.5,), offsets=(-1000.0,)),
    ),
    (
        "uint16_three_band_scaled",
        np.uint16,
        3,
        dict(compress="lzw", scales=(1.0, 0.25, 2.0), offsets=(0.0, 10.0, -5.5)),
    ),
]


def write(path, dtype, bands, options):
    options = dict(options)
    transform = options.pop("transform", NORTH_UP)
    area_or_point = options.pop("area_or_point", None)
    scales = options.pop("scales", None)
    offsets = options.pop("offsets", None)
    crs = options.pop("crs", MOON)
    profile = dict(driver="GTiff", width=WIDTH, height=HEIGHT, count=bands, dtype=dtype)
    if transform is not None:
        profile["transform"] = transform
    if crs is not None:
        profile["crs"] = crs
    profile.update({k: v for k, v in options.items() if v is not None})
    data = pattern(dtype, bands)
    if "nodata" in options:
        data[0, 3, 4] = options["nodata"]
    with rasterio.Env(GTIFF_POINT_GEO_IGNORE=False):
        with rasterio.open(path, "w", **profile) as dataset:
            if area_or_point:
                dataset.update_tags(AREA_OR_POINT=area_or_point)
            if scales:
                dataset.scales = scales
                dataset.offsets = offsets
            dataset.write(data)


def cpp_float(value):
    if not math.isfinite(value):
        return "NAN"
    text = f"{float(value):.9g}"
    if "." not in text and "e" not in text:
        text += ".0"
    return text + "f"


def cpp_floats(values):
    return ", ".join(cpp_float(v) for v in values)


def main():
    lines = [
        "// SPDX-License-Identifier: BSD-3-Clause",
        "",
        f"// Generated by generate_geotiff_fixtures.py with GDAL {rasterio.__gdal_version__}; do not edit.",
        "",
    ]
    with tempfile.TemporaryDirectory() as directory:
        for name, dtype, bands, options in FIXTURES:
            path = Path(directory) / f"{name}.tif"
            write(path, dtype, bands, options)
            raw = path.read_bytes()
            with rasterio.open(path) as dataset:
                band = 1 if bands == 3 else 0
                values = dataset.read(band + 1).astype(np.float32).ravel()
                transform = dataset.transform if dataset.transform != Affine.identity() else None
                nodata = dataset.nodata
                scales = dataset.scales
                offsets = dataset.offsets
            data = ", ".join(f"0x{b:02x}" for b in raw)
            lines += [
                f"const GdalFixture {name}{{",
                f'    "{name}", {band}, {WIDTH}, {HEIGHT}, {bands},',
                f"    std::vector<std::uint8_t>{{{data}}},",
                f"    std::vector<float>{{{cpp_floats(values)}}},",
                (
                    "    std::array<double, 6>{"
                    + ", ".join(
                        repr(float(v))
                        for v in (
                            transform.c,
                            transform.a,
                            transform.b,
                            transform.f,
                            transform.d,
                            transform.e,
                        )
                    )
                    + "}, true,"
                    if transform
                    else "    std::array<double, 6>{0, 1, 0, 0, 0, 1}, false,"
                ),
                f"    {repr(float(nodata)) if nodata is not None else 'std::nullopt'},",
                "    std::vector<double>{" + ", ".join(repr(float(v)) for v in scales) + "},",
                "    std::vector<double>{" + ", ".join(repr(float(v)) for v in offsets) + "}};",
                "",
            ]
    names = ", ".join(f"&{name}" for name, *_ in FIXTURES)
    lines += [f"const std::vector<const GdalFixture*> kGdalFixtures{{{names}}};", ""]
    OUTPUT.write_text("\n".join(lines))


if __name__ == "__main__":
    main()
