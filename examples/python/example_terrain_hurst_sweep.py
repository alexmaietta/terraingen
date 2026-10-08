"""Vary the Hurst exponent of a noise layer and see how roughness changes.

The gain between octaves is lacunarity^-H (`gain_from_hurst`); a lower H keeps more energy in the
short wavelengths, so the surface is rougher at short baselines. Each terrain uses the same seed,
so only H changes. Prints the RMS slope of each and writes a montage of hillshades.

    python example_terrain_hurst_sweep.py [OUTPUT_DIR]
"""

import numpy as np

import terraingen
from _common import hillshade, montage, output_dir, write_png

PIXEL = 2.0


def main():
    out = output_dir()
    shades = []
    for hurst in (0.3, 0.5, 0.7, 0.9):
        params = terraingen.FractalParams(seed=4, wavelength=400.0, amplitude=20.0, gain=terraingen.gain_from_hurst(hurst))
        terrain = terraingen.Terrain(
            terraingen.FlatSource(257, 257, PIXEL), [terraingen.NoiseRelief([terraingen.NoiseLayer(params)])]
        )
        heights = terraingen.evaluate(terrain).heights.values.astype(np.float64)
        dz_dy, dz_dx = np.gradient(heights, PIXEL)
        rms = np.degrees(np.arctan(np.sqrt(np.mean(dz_dx**2 + dz_dy**2))))
        print(f"H = {hurst:.1f}: RMS slope {rms:5.2f} degrees over {PIXEL} m")
        shades.append(hillshade(heights, PIXEL))
    print("wrote", write_png(out / "example_terrain_hurst_sweep.png", montage(shades, columns=4)))


if __name__ == "__main__":
    main()
