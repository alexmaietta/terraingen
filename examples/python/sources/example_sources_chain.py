"""Fill gaps in a DEM: a source chain falls back to the next source where one has no data.

`SourceChain` tries its sources in order at each cell, so a DEM with holes (here a 2 m DEM with a
missing strip and a corner of no-data) is filled by the next source, a synthetic terrain in this
example, on the first source's grid. Writes the heights and a hillshade.

    python example_sources_chain.py [OUTPUT_DIR]
"""

import pathlib
import sys

import numpy as np

import terraingen

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
from _common import hillshade, output_dir, write_png

PIXEL = 2.0


def holed_dem(path):
    terrain = terraingen.Terrain(
        terraingen.FlatSource(257, 257, PIXEL),
        [terraingen.NoiseRelief([terraingen.NoiseLayer(terraingen.FractalParams(seed=7, wavelength=300.0, amplitude=20.0))])],
    )
    heights = terraingen.evaluate(terrain).heights
    values = heights.values
    values[:, 100:130] = np.nan
    values[200:, 200:] = np.nan
    terraingen.write_geotiff(path, heights)
    return np.isnan(values).sum()


def main():
    out = output_dir()
    holes = holed_dem(out / "example_sources_chain_holed.tif")
    dem = terraingen.GeoTiffSource(out / "example_sources_chain_holed.tif")
    filler = terraingen.FlatSource(400, 400, PIXEL)
    chain = terraingen.SourceChain([dem, filler])
    noise = terraingen.NoiseRelief([terraingen.NoiseLayer(terraingen.FractalParams(seed=7, wavelength=300.0, amplitude=20.0))])
    filled = terraingen.evaluate(terraingen.Terrain(chain, [noise])).heights.values
    print(f"{holes} cells without data in the DEM; {np.isnan(filled).sum()} after the chain")
    print("wrote", write_png(out / "example_sources_chain.png", hillshade(filled, PIXEL)))


if __name__ == "__main__":
    main()
