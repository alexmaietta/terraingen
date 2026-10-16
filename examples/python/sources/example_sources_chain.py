"""Fill gaps in a DEM: a source chain falls back to the next source where one has no data.

`SourceChain` tries its sources in order at each cell, so a DEM with holes (here a 2 m DEM with a
missing strip and a corner of no-data) is filled by the next source, a synthetic terrain in this
example, on the first source's grid. Writes the heights and a hillshade.

    python example_sources_chain.py [--viewer window|null] [--output-dir DIR] [--test]
"""

import pathlib
import sys
import tempfile

import numpy as np

import terraingen

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
import _common
from _common import hillshade, write_png

PIXEL = 2.0


def holed_dem():
    """A 2 m DEM with a missing strip and a corner of no-data."""
    terrain = terraingen.Terrain(
        terraingen.FlatSource(257, 257, PIXEL),
        [
            terraingen.NoiseRelief(
                [terraingen.NoiseLayer(terraingen.FractalParams(seed=7, wavelength=300.0, amplitude=20.0))]
            )
        ],
    )
    heights = terraingen.evaluate(terrain)
    values = heights.values
    values[:, 100:130] = np.nan
    values[200:, 200:] = np.nan
    return heights


class Example(_common.Example):
    def generate(self):
        self.holed = holed_dem()
        self.holes = np.isnan(self.holed.values).sum()
        with tempfile.TemporaryDirectory() as temp:
            path = pathlib.Path(temp) / "example_sources_chain_holed.tif"
            terraingen.write_geotiff(path, self.holed)
            dem = terraingen.GeoTiffSource(path)
            filler = terraingen.FlatSource(400, 400, PIXEL)
            chain = terraingen.SourceChain([dem, filler])
            noise = terraingen.NoiseRelief(
                [terraingen.NoiseLayer(terraingen.FractalParams(seed=7, wavelength=300.0, amplitude=20.0))]
            )
            self.filled = terraingen.evaluate(terraingen.Terrain(chain, [noise]))
        print(
            f"{self.holes} cells without data in the DEM; {np.isnan(self.filled.values).sum()} after the chain"
        )
        self.show("filled", self.filled)

    def save(self, out):
        terraingen.write_geotiff(out / "example_sources_chain_holed.tif", self.holed)
        print("wrote", write_png(out / "example_sources_chain.png", hillshade(self.filled.values, PIXEL)))

    def test_final(self):
        holed = self.holed.values
        filled = self.filled.values
        assert holed.shape == (257, 257) and filled.shape == (257, 257), (
            "the chain keeps the DEM's 257 x 257 grid"
        )
        assert self.holes == 257 * 30 + 57 * 57, "the strip and the corner have no data"
        assert np.isfinite(filled).all(), "the chain leaves no cell without data"
        known = np.isfinite(holed)
        assert np.ptp(filled[known]) > 5.0 and np.ptp(filled[~known]) > 5.0, (
            "relief inside and outside the holes"
        )


if __name__ == "__main__":
    _common.main(Example)
