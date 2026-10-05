"""Checks TerrainGen's GeoTIFF reader and writer against GDAL.

GDAL writes GeoTIFFs in a range of layouts and lunar projections; terraingen_geotiff_roundtrip reads
each with TerrainGen and writes it back; GDAL then reads TerrainGen's file, which must have the same
values (as float32), transform, CRS and no-data as GDAL's view of the original.

Usage: check_geotiff_with_gdal.py ROUNDTRIP_EXECUTABLE WORK_DIRECTORY
"""

import subprocess
import sys
from pathlib import Path

import numpy as np
import rasterio
from rasterio.crs import CRS
from rasterio.transform import Affine

WIDTH, HEIGHT = 300, 217

CASES = {
    "moon_equirectangular_lzw": dict(
        crs=CRS.from_proj4("+proj=eqc +R=1737400 +units=m +no_defs"), dtype="float32",
        compress="lzw", predictor=3, tiled=True, blockxsize=64, blockysize=64),
    "moon_south_polar_stereographic_int16": dict(
        crs=CRS.from_proj4("+proj=stere +lat_0=-90 +lon_0=0 +k=1 +R=1737400 +units=m +no_defs"),
        dtype="int16", compress="lzw", predictor=2, nodata=-32768),
    "moon_iau_2015_geographic": dict(crs=CRS.from_wkt(
        'GEOGCRS["Moon (2015) - Sphere / Ocentric",DATUM["Moon (2015) - Sphere",'
        'ELLIPSOID["Moon (2015) - Sphere",1737400,0,LENGTHUNIT["metre",1]]],'
        'PRIMEM["Reference Meridian",0,ANGLEUNIT["degree",0.0174532925199433]],'
        'CS[ellipsoidal,2],AXIS["latitude",north,ORDER[1],ANGLEUNIT["degree",0.0174532925199433]],'
        'AXIS["longitude",east,ORDER[2],ANGLEUNIT["degree",0.0174532925199433]],'
        'ID["IAU",30100,2015]]'), dtype="float64", transform=Affine(0.01, 0, 10, 0, -0.01, -80)),
    "earth_utm_big_endian_point": dict(
        crs=CRS.from_epsg(32613), dtype="float32", ENDIANNESS="BIG", area_or_point="Point"),
    "rotated_uint16_packbits": dict(
        crs=CRS.from_epsg(4326), dtype="uint16", compress="packbits",
        transform=Affine(0.001, 0.0002, -105, 0.0001, -0.001, 40)),
    "bigtiff_nodata_float32": dict(
        crs=CRS.from_epsg(4326), dtype="float32", compress="lzw", BIGTIFF="YES", nodata=-9999.0),
}


def write_input(path, case):
    case = dict(case)
    dtype = case.pop("dtype")
    transform = case.pop("transform", Affine(5.0, 0, -7500.0, 0, -5.0, 12000.0))
    area_or_point = case.pop("area_or_point", None)
    y, x = np.mgrid[0:HEIGHT, 0:WIDTH]
    values = 300 * np.sin(x / 23.0) * np.cos(y / 31.0) + x * 0.7 - y * 0.3
    if np.issubdtype(np.dtype(dtype), np.integer):
        values = np.round(values + (200 if dtype.startswith("u") else 0))
    data = values.astype(dtype)
    if "nodata" in case:
        data[10:20, 30:40] = case["nodata"]
    profile = dict(driver="GTiff", width=WIDTH, height=HEIGHT, count=1, dtype=dtype,
                   transform=transform, **case)
    with rasterio.open(path, "w", **profile) as dataset:
        if area_or_point:
            dataset.update_tags(AREA_OR_POINT=area_or_point)
        dataset.write(data, 1)


def main():
    roundtrip, work = sys.argv[1], Path(sys.argv[2])
    work.mkdir(parents=True, exist_ok=True)
    failures = []
    for name, case in CASES.items():
        for compression in ("lzw", "none"):
            source = work / f"{name}.tif"
            output = work / f"{name}_terraingen_{compression}.tif"
            write_input(source, case)
            result = subprocess.run([roundtrip, str(source), str(output), compression],
                                    capture_output=True, text=True)
            if result.returncode != 0:
                failures.append(f"{name}/{compression}: roundtrip failed: {result.stderr.strip()}")
                continue
            with rasterio.open(source) as gdal, rasterio.open(output) as ours:
                problems = []
                if not np.array_equal(gdal.read(1).astype(np.float32), ours.read(1),
                                      equal_nan=True):
                    problems.append("values differ")
                if gdal.transform != ours.transform:
                    problems.append(f"transform {ours.transform} != {gdal.transform}")
                if gdal.crs != ours.crs:
                    problems.append(f"CRS {ours.crs} != {gdal.crs}")
                if gdal.nodata != ours.nodata:
                    problems.append(f"nodata {ours.nodata} != {gdal.nodata}")
                if gdal.tags().get("AREA_OR_POINT") != ours.tags().get("AREA_OR_POINT"):
                    problems.append("AREA_OR_POINT differs")
            status = "ok" if not problems else "; ".join(problems)
            print(f"{name}/{compression}: {status}")
            if problems:
                failures.append(f"{name}/{compression}: {status}")
    if failures:
        print("\n".join(["FAILED:"] + failures))
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
