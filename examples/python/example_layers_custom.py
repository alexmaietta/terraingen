"""Sources, layers, filters and albedo of your own, written in Python.

Every part of a terrain can be replaced by subclassing its base class:

- `Source`: `grid()` and `height_at(x, y)`; here an inclined plane.
- `Layer`: `apply_at(height, x, y, context)` for one point, or `apply(heights, context)` for a whole
  raster with NumPy; here a sinuous rille (point-wise) and a regional tilt (raster).
- `Filter`: `apply(heights, spacing)` after every layer; here a terrace quantization.
- `AlbedoSource` and `AlbedoLayer`: `value_at` and `apply_at`; here brighter albedo on slopes.

They mix freely with the built-in parts. Python parts run under the GIL, so they are slower than
the built-in ones, especially point-wise; prefer `apply` with NumPy for whole rasters.

    python example_layers_custom.py [OUTPUT_DIR]
"""

import math

import numpy as np

import terraingen
from _common import hillshade, montage, normalize, output_dir, write_png

PIXEL = 2.0
SIZE = 257


class InclinedPlane(terraingen.Source):
    """A plane rising `slope` metres per metre towards the north-east."""

    def __init__(self, slope=0.02):
        super().__init__()
        self.slope = slope

    def grid(self):
        return terraingen.RasterGrid.centred(SIZE, SIZE, PIXEL)

    def height_at(self, x, y):
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


class Terraces(terraingen.Filter):
    """Rounds heights to steps of `step` metres, softened by half."""

    def __init__(self, step=4.0):
        super().__init__()
        self.step = step

    def apply(self, heights, spacing):
        values = heights.values
        values[:] = 0.5 * values + 0.5 * np.round(values / self.step) * self.step


class SlopeBrightening(terraingen.AlbedoLayer):
    """Brighter albedo where the rille's walls are steep (fresh material exposed)."""

    def apply_at(self, value, bands, x, y, context):
        centre = 60.0 * math.sin(y / 70.0)
        wall = abs(x - centre) / 20.0
        brightening = 1.0 + 0.6 * wall * math.exp(-(wall**2))
        return [v * brightening for v in value]


def main():
    out = output_dir()
    fbm = terraingen.NoiseLayer(terraingen.FractalParams(seed=1, wavelength=200.0, amplitude=4.0))
    surface = terraingen.lunar_surface(3.5)
    models = terraingen.CraterModels(
        surface.density, surface.ages, terraingen.FreshnessDegradation(surface.ages), terraingen.FassettThomsonProfile()
    )
    generation = terraingen.CraterGenerationOptions(min_diameter=8.0, max_diameter=80.0)
    craters = terraingen.CraterLayer(models, terraingen.CraterLayerOptions(generation=generation))

    plain = terraingen.Terrain(InclinedPlane(), [terraingen.NoiseRelief([fbm]), Rille(), Tilt(), craters])
    terraced = terraingen.Terrain(plain.source, plain.layers, [Terraces()])
    shades = []
    for name, terrain in (("custom source and layers", plain), ("plus a custom filter", terraced)):
        heights = terraingen.evaluate(terrain).heights.values
        print(f"{name:26s} heights {heights.min():7.2f} to {heights.max():6.2f} m")
        shades.append(hillshade(heights, PIXEL))
    print(f"height at the origin: {terraingen.evaluate_at(plain, 0.0, 0.0, PIXEL):.2f} m")

    albedo = terraingen.Albedo(terraingen.SyntheticAlbedo(value=0.11), [SlopeBrightening()])
    band = terraingen.evaluate_albedo(albedo, plain.source.grid())[0].values
    print(f"albedo from {band.min():.3f} to {band.max():.3f}")
    shades.append(normalize(band))
    print("wrote", write_png(out / "example_layers_custom.png", montage(shades, columns=3)))


if __name__ == "__main__":
    main()
