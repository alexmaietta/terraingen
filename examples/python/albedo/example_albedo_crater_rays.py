"""Albedo for a cratered synthetic terrain, with bright crater rays.

A `SyntheticAlbedo` of 0.11 with gentle noise, and a `CraterAlbedo` layer that reads the terrain's
crater layer: fresh craters are brighter (optical maturity), and young ones throw rays whose
length follows Elliott et al. (2018). A hand-placed 2 Myr old crater shows the rays. Writes the
albedo as a GeoTIFF and PNGs of the albedo and the hillshade. The same terrain as
examples/config/albedo/example_albedo_crater_rays.json.

    python example_albedo_crater_rays.py [--viewer window|null] [--output-dir DIR] [--test]
"""

import pathlib
import sys

import numpy as np

import terraingen

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
import _common
from _common import hillshade, normalize, write_png

PIXEL = 8.0


class Example(_common.Example):
    def generate(self):
        fbm = terraingen.NoiseLayer(terraingen.FractalParams(seed=1, wavelength=3000.0, amplitude=60.0))
        surface = terraingen.lunar_surface(3.2)
        models = terraingen.CraterModels(surface.density, surface.ages, terraingen.FreshnessDegradation(surface.ages), terraingen.FassettThomsonProfile())
        young = terraingen.Crater(0.0, 0.0, 250.0, age_gyr=0.002)
        craters = terraingen.CraterLayer(
            models, terraingen.CraterLayerOptions(seed=1, generation=terraingen.CraterGenerationOptions(min_diameter=30.0, max_diameter=400.0, manual=[young]))
        )
        terrain = terraingen.Terrain(terraingen.FlatSource(513, 513, PIXEL), [terraingen.NoiseRelief([fbm]), craters])

        variation = terraingen.NoiseLayer(terraingen.FractalParams(seed=4, wavelength=1500.0, amplitude=0.02, octaves=4))
        albedo = terraingen.Albedo(terraingen.SyntheticAlbedo(value=0.11, value_noise=[variation]), [terraingen.CraterAlbedo(craters)])

        self.evaluated = terraingen.evaluate(terrain)
        self.bands = terraingen.evaluate_albedo(albedo, terrain.source.grid())
        values = self.bands[0].values
        print(f"albedo from {values.min():.3f} to {values.max():.3f} over {len(self.evaluated.craters['x'])} craters")
        self.show("heights and albedo", self.evaluated.heights, albedo=self.bands[0])

    def save(self, out):
        terraingen.write_geotiff(out / "example_albedo_crater_rays.tif", self.bands)
        print("wrote", write_png(out / "example_albedo_crater_rays.png", normalize(self.bands[0].values)))
        print("wrote", write_png(out / "example_albedo_crater_rays_shade.png", hillshade(self.evaluated.heights.values, PIXEL)))

    def test_final(self):
        values = self.bands[0].values
        assert self.evaluated.heights.shape == (513, 513)
        assert len(self.bands) == 1 and values.shape == (513, 513), "one albedo band on the heights' grid"
        assert np.all(np.isfinite(values)), "the albedo is finite"
        assert values.min() > 0.08 and values.max() < 0.2, "the albedo stays near 0.11"
        assert values.max() > values.mean() + 0.005, "fresh craters brighten the albedo"
        assert len(self.evaluated.craters["x"]) > 500, "more than 500 craters"


if __name__ == "__main__":
    _common.main(Example)
