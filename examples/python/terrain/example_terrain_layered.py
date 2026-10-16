"""A stack of noise layers, each combined with the heights so far by its blend mode.

Rolling fBm, domain-warped ridged crests added on top (their octave gain set from a Hurst
exponent), and broad value noise as a mask that roughens some regions and calms others. The same
terrain as examples/config/terrain/example_terrain_layered.json, plus the warp.

    python example_terrain_layered.py [--viewer window|null] [--output-dir DIR] [--test]
"""

import pathlib
import sys

import numpy as np

import terraingen

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
import _common
from _common import hillshade, write_png


class Example(_common.Example):
    def generate(self):
        rolling = terraingen.NoiseLayer(terraingen.FractalParams(seed=1, wavelength=800.0, amplitude=40.0))
        crests = terraingen.NoiseLayer(
            terraingen.FractalParams(
                seed=2,
                type=terraingen.FractalType.RIDGED,
                wavelength=250.0,
                amplitude=8.0,
                octaves=6,
                gain=terraingen.gain_from_hurst(0.7),
            ),
            blend=terraingen.LayerBlend.ADD,
            warp_amplitude=60.0,
            warp_wavelength=400.0,
        )
        mask = terraingen.NoiseLayer(
            terraingen.FractalParams(
                seed=3, basis=terraingen.NoiseBasis.VALUE, wavelength=1200.0, amplitude=1.0, octaves=2
            ),
            blend=terraingen.LayerBlend.MASK,
        )
        source = terraingen.FlatSource(513, 513, 2.0)
        terrain = terraingen.Terrain(source, [terraingen.NoiseRelief([rolling, crests, mask])])

        self.evaluated = terraingen.evaluate(terrain)
        self.rolling = terraingen.evaluate(
            terraingen.Terrain(source, [terraingen.NoiseRelief([rolling])])
        ).heights
        heights = self.evaluated.heights.values
        print(f"heights from {heights.min():.2f} to {heights.max():.2f} m")
        self.show("layered", self.evaluated.heights)
        self.show("rolling fBm only", self.rolling)

    def save(self, out):
        terraingen.write_geotiff(out / "example_terrain_layered.tif", self.evaluated.heights)
        print(
            "wrote",
            write_png(
                out / "example_terrain_layered.png",
                hillshade(self.evaluated.heights.values, 2.0, exaggeration=3.0),
            ),
        )

    def test_final(self):
        heights = self.evaluated.heights.values
        assert heights.shape == (513, 513), "the heights are 513 x 513"
        assert np.all(np.isfinite(heights)), "every height is finite"
        assert 10.0 < heights.max() - heights.min() < 500.0, "the relief is tens of metres"
        assert np.abs(heights - self.rolling.values).max() > 0.5, "the crests and mask change the rolling fBm"


if __name__ == "__main__":
    _common.main(Example)
