"""Add detail to a coarse DEM: a GeoTIFF source resampled finer, plus noise and small craters.

A coarse elevation model, such as one from orbit, is read with `GeoTiffSource`, interpolated to a
finer pixel size, and given what it cannot resolve: fine noise and craters smaller than its
pixels. This example first writes a coarse 16 m DEM of its own to stand in for real data, then
builds the detailed 2 m terrain on it. Writes both and their hillshades.

    python example_sources_raster_detail.py [OUTPUT_DIR]
"""

import pathlib
import sys

import terraingen

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
from _common import hillshade, montage, output_dir, write_png


def coarse_dem(path):
    """A 16 m DEM standing in for orbital data: broad relief and large craters."""
    surface = terraingen.lunar_surface(3.5)
    models = terraingen.CraterModels(
        surface.density, surface.ages, terraingen.FreshnessDegradation(surface.ages), terraingen.FassettThomsonProfile()
    )
    generation = terraingen.CraterGenerationOptions(min_diameter=100.0, max_diameter=600.0)
    terrain = terraingen.Terrain(
        terraingen.FlatSource(65, 65, 16.0),
        [
            terraingen.NoiseRelief([terraingen.NoiseLayer(terraingen.FractalParams(seed=4, wavelength=800.0, amplitude=40.0))]),
            terraingen.CraterLayer(models, terraingen.CraterLayerOptions(seed=4, generation=generation)),
        ],
    )
    heights = terraingen.evaluate(terrain).heights
    terraingen.write_geotiff(path, heights)
    return heights


def main():
    out = output_dir()
    dem = coarse_dem(out / "example_sources_raster_detail_coarse.tif")
    source = terraingen.GeoTiffSource(
        out / "example_sources_raster_detail_coarse.tif", pixel_size=2.0, resampling=terraingen.ResampleMethod.CUBIC
    )
    print(f"coarse DEM {dem.width} x {dem.height} at 16 m; source resampled to {source.width} x {source.height} at 2 m")
    surface = terraingen.lunar_surface(3.5)
    models = terraingen.CraterModels(
        surface.density, surface.ages, terraingen.FreshnessDegradation(surface.ages), terraingen.FassettThomsonProfile()
    )
    small = terraingen.CraterLayer(
        models, terraingen.CraterLayerOptions(seed=5, generation=terraingen.CraterGenerationOptions(min_diameter=6.0, max_diameter=100.0))
    )
    detail = terraingen.NoiseRelief([terraingen.NoiseLayer(terraingen.FractalParams(seed=6, wavelength=60.0, amplitude=1.5, octaves=5))])
    evaluated = terraingen.evaluate(terraingen.Terrain(source, [detail, small]))
    print(f"added {len(evaluated.craters['x'])} craters of 6 to 100 m")
    terraingen.write_geotiff(out / "example_sources_raster_detail.tif", evaluated.heights)
    coarse = terraingen.evaluate(terraingen.Terrain(source)).heights.values
    shades = [hillshade(coarse, 2.0), hillshade(evaluated.heights.values, 2.0)]
    print("wrote", write_png(out / "example_sources_raster_detail.png", montage(shades, columns=2)))


if __name__ == "__main__":
    main()
