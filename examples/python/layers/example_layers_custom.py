"""Sources, layers and albedo of your own, written in Python.

Every part of a terrain can be replaced by subclassing its base class:

- `Source`: `grid()` and `height_at(x, y, spacing)`; here an inclined plane.
- `Layer`: `apply_at(height, x, y, context)` for one point, or `apply(heights, context)` for a whole
  raster with NumPy; here a sinuous rille (point-wise), a regional tilt (raster) and a terrace
  quantization (raster).
- `AlbedoSource` and `AlbedoLayer`: `value_at` and `apply_at`; here brighter albedo on slopes.

They mix freely with the built-in parts. Python parts run under the GIL, so they are slower than
the built-in ones, especially point-wise; prefer `apply` with NumPy for whole rasters.

    python example_layers_custom.py [--viewer window|null] [--output-dir DIR] [--test]
"""

import math
import pathlib
import sys

import numpy as np

import terraingen

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
import _common
from _common import hillshade, montage, normalize, write_png

PIXEL = 2.0
SIZE = 257


class InclinedPlane(terraingen.Source):
    """A plane rising `slope` metres per metre towards the north-east."""

    def __init__(self, slope=0.02):
        super().__init__()
        self.slope = slope

    def grid(self):
        return terraingen.RasterGrid.centred(SIZE, SIZE, PIXEL)

    def height_at(self, x, y, spacing):
        return self.slope * (x + y) / math.sqrt(2.0)


class Rille(terraingen.Layer):
    """A sinuous channel 12 m deep and 40 m wide, meandering north to south."""

    def apply_at(self, height, x, y, context):
        centre = 60.0 * math.sin(y / 70.0)
        return height - 12.0 * math.exp(-(((x - centre) / 20.0) ** 2))

    def max_feature_diameter(self):
        return 40.0


class Tilt(terraingen.Layer):
    """A tilt applied to the whole raster at once with NumPy."""

    def apply(self, heights, context):
        heights.values[:] += np.linspace(-5.0, 5.0, heights.width, dtype=np.float32)[None, :]


class Terraces(terraingen.Layer):
    """Rounds heights to steps of `step` metres, softened by half."""

    def __init__(self, step=4.0):
        super().__init__()
        self.step = step

    def apply(self, heights, context):
        values = heights.values
        values[:] = 0.5 * values + 0.5 * np.round(values / self.step) * self.step


class SlopeBrightening(terraingen.AlbedoLayer):
    """Brighter albedo where the rille's walls are steep (fresh material exposed)."""

    def apply_at(self, value, bands, x, y, context):
        centre = 60.0 * math.sin(y / 70.0)
        wall = abs(x - centre) / 20.0
        brightening = 1.0 + 0.6 * wall * math.exp(-(wall**2))
        return [v * brightening for v in value]


class Example(_common.Example):
    def generate(self):
        fbm = terraingen.NoiseLayer(terraingen.FractalParams(seed=1, wavelength=200.0, amplitude=4.0))
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
        generation = terraingen.CraterGenerationOptions(d_min=8.0, d_max=80.0)
        craters = terraingen.CraterLayer(models, terraingen.CraterLayerOptions(generation=generation))

        plain = terraingen.TerrainGenerator(
            InclinedPlane(), [terraingen.NoiseRelief([fbm]), Rille(), Tilt(), craters]
        )
        terraced = terraingen.TerrainGenerator(plain.source, [*plain.layers, Terraces()])
        self.heights = {}
        for name, terrain in (("custom source and layers", plain), ("plus terraces", terraced)):
            self.heights[name] = terraingen.evaluate(terrain)
            values = self.heights[name].values
            print(f"{name:26s} heights {values.min():7.2f} to {values.max():6.2f} m")
        self.origin_height = terraingen.height_at(plain, 0.0, 0.0, PIXEL)
        print(f"height at the origin: {self.origin_height:.2f} m")

        albedo = terraingen.Albedo(terraingen.SyntheticAlbedo(value=0.11), [SlopeBrightening()])
        self.albedo = terraingen.evaluate_albedo(albedo, plain.source.grid())[0]
        band = self.albedo.values
        print(f"albedo from {band.min():.3f} to {band.max():.3f}")
        self.show("heights", self.heights["custom source and layers"], albedo=self.albedo)
        self.show("terraced", self.heights["plus terraces"], albedo=self.albedo)

    def save(self, out):
        shades = [hillshade(heights.values, PIXEL) for heights in self.heights.values()] + [
            normalize(self.albedo.values)
        ]
        print("wrote", write_png(out / "example_layers_custom.png", montage(shades, columns=3)))

    def test_final(self):
        plain = self.heights["custom source and layers"].values
        terraced = self.heights["plus terraces"].values
        assert plain.shape == (SIZE, SIZE) and terraced.shape == (SIZE, SIZE), "both terrains are 257 x 257"
        assert np.isfinite(plain).all() and np.isfinite(self.origin_height), "the heights are finite"
        assert plain[:64, -64:].mean() > plain[-64:, :64].mean() + 5.0, (
            "the plane rises from the south-west to the north-east"
        )
        shift = np.abs(terraced - plain)
        assert shift.max() <= 1.0 + 1e-3 and shift.max() > 0.0, (
            "the terraces move heights by up to a quarter of their 4 m step"
        )
        band = self.albedo.values
        assert band.shape == (SIZE, SIZE) and band.min() >= 0.11 - 1e-4 and band.max() > 0.12, (
            "slopes brighten the 0.11 albedo"
        )


if __name__ == "__main__":
    _common.main(Example)
