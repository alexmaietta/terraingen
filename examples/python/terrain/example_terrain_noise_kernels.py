"""Use the noise kernels directly, without a terrain.

The kernels behind `NoiseRelief` are available as functions: single-octave bases
(`perlin_noise2`, `simplex_noise2`, `value_noise2`, and `worley2`, whose third value is the
Voronoi cell value) and fractals built from them (`fractal2` with `FractalParams`). This example
samples each over the same window and writes them side by side.

    python example_terrain_noise_kernels.py [--viewer window|null] [--output-dir DIR] [--test]
"""

import pathlib
import sys

import numpy as np

import terraingen

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
import _common
from _common import montage, normalize, write_png

SIZE = 160


def sample(function):
    """`function(x, y)` over a SIZE x SIZE window of lattice coordinates 0 to 8."""
    coordinates = np.linspace(0.0, 8.0, SIZE, dtype=np.float32)
    return np.array([[function(float(x), float(y)) for x in coordinates] for y in coordinates])


class Example(_common.Example):
    def generate(self):
        bases = {
            "perlin": lambda x, y: terraingen.perlin_noise2(x, y, 1),
            "simplex": lambda x, y: terraingen.simplex_noise2(x, y, 1),
            "value": lambda x, y: terraingen.value_noise2(x, y, 1),
            "worley F1": lambda x, y: terraingen.worley2(x, y, 1)[0],
            "voronoi": lambda x, y: terraingen.worley2(x, y, 1)[2],
        }
        self.values = {}
        for name, function in bases.items():
            values = sample(function)
            print(f"{name:20s} range {values.min():+.3f} to {values.max():+.3f}")
            self.values[name] = values
        for fractal in (
            terraingen.FractalType.FBM,
            terraingen.FractalType.RIDGED,
            terraingen.FractalType.BILLOW,
            terraingen.FractalType.HYBRID_MULTIFRACTAL,
        ):
            params = terraingen.FractalParams(seed=2, wavelength=4.0, amplitude=1.0, octaves=6, type=fractal)
            values = sample(lambda x, y, params=params: terraingen.fractal2(x, y, params))
            print(f"{fractal.name:20s} range {values.min():+.3f} to {values.max():+.3f}")
            self.values[fractal.name] = values
        spacing = 8.0 / (SIZE - 1)
        self.show("fBm", self.values["FBM"], spacing)
        self.show("perlin", self.values["perlin"], spacing)

    def save(self, out):
        tiles = [normalize(values) for values in self.values.values()]
        print("wrote", write_png(out / "example_terrain_noise_kernels.png", montage(tiles, columns=4)))

    def test_final(self):
        assert len(self.values) == 9, "five bases and four fractals"
        assert all(
            values.shape == (SIZE, SIZE) and np.all(np.isfinite(values)) for values in self.values.values()
        ), "every sample is finite"
        assert all(values.max() > values.min() for values in self.values.values()), (
            "every kernel varies over the window"
        )
        assert all(np.abs(self.values[name]).max() <= 1.5 for name in ("perlin", "simplex", "value")), (
            "the single-octave bases stay near [-1, 1]"
        )
        assert self.values["worley F1"].min() >= 0.0, "Worley F1 distances are not negative"


if __name__ == "__main__":
    _common.main(Example)
