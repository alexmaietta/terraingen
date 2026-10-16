"""One terrain per fractal type, from the same seed.

fBm sums octaves; ridged turns |noise| into sharp crests; billow gives rounded forms; the hybrid
multifractal keeps lowlands smooth and highlands rough. Writes a montage of hillshades.

    python example_terrain_fractal_types.py [--viewer window|null] [--output-dir DIR] [--test]
"""

import pathlib
import sys

import numpy as np

import terraingen

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
import _common
from _common import hillshade, montage, write_png

PIXEL = 2.0


class Example(_common.Example):
    def generate(self):
        self.heights = {}
        for fractal in (
            terraingen.FractalType.FBM,
            terraingen.FractalType.RIDGED,
            terraingen.FractalType.BILLOW,
            terraingen.FractalType.HYBRID_MULTIFRACTAL,
        ):
            layer = terraingen.NoiseLayer(
                terraingen.FractalParams(seed=1, type=fractal, wavelength=400.0, amplitude=30.0)
            )
            terrain = terraingen.Terrain(
                terraingen.FlatSource(257, 257, PIXEL), [terraingen.NoiseRelief([layer])]
            )
            raster = terraingen.evaluate(terrain)
            values = raster.values
            print(f"{fractal.name:20s} mean {values.mean():6.2f} m, std {values.std():5.2f} m")
            self.heights[fractal.name] = values
            self.show(fractal.name.lower(), raster)

    def save(self, out):
        shades = [hillshade(values, PIXEL, exaggeration=2.0) for values in self.heights.values()]
        print("wrote", write_png(out / "example_terrain_fractal_types.png", montage(shades, columns=4)))

    def test_final(self):
        assert len(self.heights) == 4, "one terrain per fractal type"
        assert all(
            values.shape == (257, 257) and np.all(np.isfinite(values)) for values in self.heights.values()
        ), "every terrain is 257 x 257 and finite"
        assert all(values.std() > 0.5 for values in self.heights.values()), "every fractal type has relief"
        fbm = self.heights["FBM"]
        assert all(not np.allclose(fbm, values) for name, values in self.heights.items() if name != "FBM"), (
            "the types differ from fBm"
        )


if __name__ == "__main__":
    _common.main(Example)
