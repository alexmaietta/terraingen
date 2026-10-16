"""Add detail to a coarse DEM: a GeoTIFF source resampled finer, plus noise and small craters.

A coarse elevation model, such as one from orbit, is read with `GeoTiffSource`, interpolated to a
finer pixel size, and given what it cannot resolve: fine noise and craters smaller than its
pixels. This example first writes a coarse 16 m DEM of its own to stand in for real data, then
builds the detailed 2 m terrain on it. Writes both and their hillshades.

    python example_sources_raster_detail.py [--viewer window|null] [--output-dir DIR] [--test]
"""

import pathlib
import sys
import tempfile

import numpy as np

import terraingen

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
import _common
from _common import hillshade, montage, write_png


def coarse_dem():
    """A 16 m DEM standing in for orbital data: broad relief and large craters."""
    production = terraingen.extended_neukum_production_function()
    equilibrium = terraingen.trask_equilibrium()
    density = terraingen.MinimumSfd(
        [terraingen.ProductionSurface(production, 3.5, terraingen.neukum_lunar_chronology), equilibrium]
    )
    ages = terraingen.SteadyStateAges(production, equilibrium, 3.5, terraingen.neukum_lunar_chronology)
    models = terraingen.CraterModels(
        density,
        ages,
        terraingen.FreshnessDegradation(ages, terraingen.lunar_fresh_crater_morphometry),
        terraingen.FassettThomsonProfile(),
    )
    generation = terraingen.CraterGenerationOptions(min_diameter=100.0, max_diameter=600.0)
    terrain = terraingen.Terrain(
        terraingen.FlatSource(65, 65, 16.0),
        [
            terraingen.NoiseRelief(
                [terraingen.NoiseLayer(terraingen.FractalParams(seed=4, wavelength=800.0, amplitude=40.0))]
            ),
            terraingen.CraterLayer(models, terraingen.CraterLayerOptions(seed=4, generation=generation)),
        ],
    )
    return terraingen.evaluate(terrain).heights


class Example(_common.Example):
    def generate(self):
        self.dem = coarse_dem()
        with tempfile.TemporaryDirectory() as temp:
            path = pathlib.Path(temp) / "example_sources_raster_detail_coarse.tif"
            terraingen.write_geotiff(path, self.dem)
            source = terraingen.GeoTiffSource(
                path, pixel_size=2.0, resampling=terraingen.ResampleMethod.CUBIC
            )
            print(
                f"coarse DEM {self.dem.width} x {self.dem.height} at 16 m; source resampled to {source.width} x {source.height} at 2 m"
            )
            production = terraingen.extended_neukum_production_function()
            equilibrium = terraingen.trask_equilibrium()
            density = terraingen.MinimumSfd(
                [
                    terraingen.ProductionSurface(production, 3.5, terraingen.neukum_lunar_chronology),
                    equilibrium,
                ]
            )
            ages = terraingen.SteadyStateAges(
                production, equilibrium, 3.5, terraingen.neukum_lunar_chronology
            )
            models = terraingen.CraterModels(
                density,
                ages,
                terraingen.FreshnessDegradation(ages, terraingen.lunar_fresh_crater_morphometry),
                terraingen.FassettThomsonProfile(),
            )
            small = terraingen.CraterLayer(
                models,
                terraingen.CraterLayerOptions(
                    seed=5,
                    generation=terraingen.CraterGenerationOptions(min_diameter=6.0, max_diameter=100.0),
                ),
            )
            detail = terraingen.NoiseRelief(
                [
                    terraingen.NoiseLayer(
                        terraingen.FractalParams(seed=6, wavelength=60.0, amplitude=1.5, octaves=5)
                    )
                ]
            )
            self.evaluated = terraingen.evaluate(terraingen.Terrain(source, [detail, small]))
            print(f"added {len(self.evaluated.craters['x'])} craters of 6 to 100 m")
            self.resampled = terraingen.evaluate(terraingen.Terrain(source)).heights
        self.show("heights", self.evaluated.heights)
        self.show("resampled DEM", self.resampled)

    def save(self, out):
        terraingen.write_geotiff(out / "example_sources_raster_detail_coarse.tif", self.dem)
        terraingen.write_geotiff(out / "example_sources_raster_detail.tif", self.evaluated.heights)
        shades = [hillshade(self.resampled.values, 2.0), hillshade(self.evaluated.heights.values, 2.0)]
        print("wrote", write_png(out / "example_sources_raster_detail.png", montage(shades, columns=2)))

    def test_final(self):
        heights = self.evaluated.heights.values
        diameters = self.evaluated.craters["diameter"]
        assert self.dem.shape == (65, 65), "the coarse DEM is 65 x 65"
        assert 512 <= heights.shape[0] <= 528 and 512 <= heights.shape[1] <= 528, (
            "the 16 m DEM resamples to about 520 x 520 cells of 2 m"
        )
        assert self.resampled.values.shape == heights.shape and np.isfinite(heights).all(), (
            "finite heights on the resampled grid"
        )
        assert len(diameters) > 500 and diameters.min() >= 6.0 and diameters.max() <= 100.0, (
            "more than 500 detail craters of 6 to 100 m"
        )
        assert np.std(np.diff(heights, axis=1)) > np.std(np.diff(self.resampled.values, axis=1)), (
            "the detail roughens the resampled DEM"
        )


if __name__ == "__main__":
    _common.main(Example)
