"""Use the noise kernels directly, without a terrain.

The kernels behind `NoiseRelief` are available as functions: single-octave bases
(`perlin_noise2`, `simplex_noise2`, `value_noise2`, `worley2`) and fractals built from them
(`fractal2` with `FractalParams`). This example samples each over the same window and writes them
side by side.

    python example_terrain_noise_kernels.py [OUTPUT_DIR]
"""

import numpy as np

import terraingen
from _common import montage, normalize, output_dir, write_png

SIZE = 160


def sample(function):
    """`function(x, y)` over a SIZE x SIZE window of lattice coordinates 0 to 8."""
    coordinates = np.linspace(0.0, 8.0, SIZE, dtype=np.float32)
    return np.array([[function(float(x), float(y)) for x in coordinates] for y in coordinates])


def main():
    out = output_dir()
    bases = {
        "perlin": lambda x, y: terraingen.perlin_noise2(x, y, 1),
        "simplex": lambda x, y: terraingen.simplex_noise2(x, y, 1),
        "value": lambda x, y: terraingen.value_noise2(x, y, 1),
        "worley F1": lambda x, y: terraingen.worley2(x, y, 1)[0],
    }
    tiles = []
    for name, function in bases.items():
        values = sample(function)
        print(f"{name:20s} range {values.min():+.3f} to {values.max():+.3f}")
        tiles.append(normalize(values))
    for fractal in (
        terraingen.FractalType.FBM,
        terraingen.FractalType.RIDGED,
        terraingen.FractalType.BILLOW,
        terraingen.FractalType.HYBRID_MULTIFRACTAL,
    ):
        params = terraingen.FractalParams(seed=2, wavelength=4.0, amplitude=1.0, octaves=6, type=fractal)
        values = sample(lambda x, y: terraingen.fractal2(x, y, params))
        print(f"{fractal.name:20s} range {values.min():+.3f} to {values.max():+.3f}")
        tiles.append(normalize(values))
    print("wrote", write_png(out / "example_terrain_noise_kernels.png", montage(tiles, columns=4)))


if __name__ == "__main__":
    main()
