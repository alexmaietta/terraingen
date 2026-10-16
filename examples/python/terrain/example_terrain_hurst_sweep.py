"""Vary the Hurst exponent of a noise layer and see how roughness changes.

The gain between octaves is lacunarity^-H (`gain_from_hurst`); a lower H keeps more energy in the
short wavelengths, so the surface is rougher at short baselines. Each terrain uses the same seed,
so only H changes. Prints the RMS slope of each and writes a montage of hillshades.

    python example_terrain_hurst_sweep.py [--viewer window|null] [--output-dir DIR] [--test]
"""

import pathlib
import sys

import numpy as np

import terraingen

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
import _common
from _common import hillshade, montage, write_png

PIXEL = 2.0
HURSTS = (0.3, 0.5, 0.7, 0.9)


class Example(_common.Example):
    def generate(self):
        self.heights = []
        self.rms = []
        for hurst in HURSTS:
            params = terraingen.FractalParams(
                seed=4, wavelength=400.0, amplitude=20.0, gain=terraingen.gain_from_hurst(hurst)
            )
            terrain = terraingen.Terrain(
                terraingen.FlatSource(257, 257, PIXEL),
                [terraingen.NoiseRelief([terraingen.NoiseLayer(params)])],
            )
            evaluated = terraingen.evaluate(terrain)
            heights = evaluated.heights.values.astype(np.float64)
            dz_dy, dz_dx = np.gradient(heights, PIXEL)
            rms = np.degrees(np.arctan(np.sqrt(np.mean(dz_dx**2 + dz_dy**2))))
            print(f"H = {hurst:.1f}: RMS slope {rms:5.2f} degrees over {PIXEL} m")
            self.heights.append(heights)
            self.rms.append(rms)
            self.show(f"H = {hurst:.1f}", evaluated.heights)

    def save(self, out):
        shades = [hillshade(heights, PIXEL) for heights in self.heights]
        print("wrote", write_png(out / "example_terrain_hurst_sweep.png", montage(shades, columns=4)))

    def test_final(self):
        assert len(self.heights) == len(HURSTS), "one terrain per Hurst exponent"
        assert all(
            heights.shape == (257, 257) and np.all(np.isfinite(heights)) for heights in self.heights
        ), "every terrain is 257 x 257 and finite"
        assert all(a > b for a, b in zip(self.rms, self.rms[1:], strict=False)), (
            "the RMS slope falls as H rises"
        )
        assert self.rms[0] > 1.2 * self.rms[-1], "H = 0.3 is clearly rougher than H = 0.9"


if __name__ == "__main__":
    _common.main(Example)
