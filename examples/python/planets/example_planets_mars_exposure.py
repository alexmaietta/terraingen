"""Mars: sand where regolith collects, rocks where bedrock is exposed.

An exposure field estimates how exposed the ground is, from 0 (deep cover in hollows) to 1 (bare
crests and steep slopes). Bedforms with `exposure` set grow only on covered ground, and rocks with
`exposure` set gather on exposed ground (up to `bedrock_abundance` times more). Prints how much of
each lies on exposed ground and writes the heights, the exposure map and a hillshade.

    python example_planets_mars_exposure.py [OUTPUT_DIR]
"""

import pathlib
import sys

import numpy as np

import terraingen

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
from _common import hillshade, montage, output_dir, write_png

PIXEL = 0.25
SIZE = 513


def main():
    out = output_dir()
    relief = terraingen.NoiseRelief([terraingen.NoiseLayer(terraingen.FractalParams(seed=3, wavelength=60.0, amplitude=5.0, octaves=7))])
    exposure = terraingen.ExposureParams(reach=8.0)
    sand = terraingen.BedformLayer(
        terraingen.BedformOptions(
            wavelength=2.5, height=0.25, wind_azimuth=60.0, cover=0.8, field_size=30.0, seed=3, exposure=exposure
        )
    )
    rocks = terraingen.RockLayer(
        terraingen.RockLayerOptions(seed=3, coverage=0.02, min_diameter=0.3, max_diameter=3.0, exposure=exposure, bedrock_abundance=4.0)
    )
    source = terraingen.FlatSource(SIZE, SIZE, PIXEL)
    terrain = terraingen.Terrain(source, [relief, sand, rocks])
    grid = source.grid()
    exposure_map = terraingen.ExposureField(exposure).fill(terraingen.Terrain(source, [relief]), 1, grid, PIXEL).values
    evaluated = terraingen.evaluate(terrain)

    covered = exposure_map < 0.5
    xs = (np.arange(SIZE) - SIZE / 2 + 0.5) * PIXEL
    ground = terraingen.Terrain(source, [relief])
    sandy = np.array([[sand.cover_at(x, -y, ground, 1) > 0 for x in xs[::8]] for y in xs[::8]])
    rows = np.clip(((grid.transform.origin_y - evaluated.rocks["y"]) / PIXEL).astype(int), 0, SIZE - 1)
    cols = np.clip(((evaluated.rocks["x"] - grid.transform.origin_x) / PIXEL).astype(int), 0, SIZE - 1)
    print(f"{covered.mean():.0%} of the ground is mostly covered")
    print(f"bedform fields cover {sandy.mean():.0%} of the area")
    print(f"{len(cols)} rocks, {np.mean(~covered[rows, cols]):.0%} of them on exposed ground")
    terraingen.write_geotiff(out / "example_planets_mars_exposure.tif", evaluated.heights)
    images = [exposure_map, hillshade(evaluated.heights.values, PIXEL)]
    print("wrote", write_png(out / "example_planets_mars_exposure.png", montage(images, columns=2)))


if __name__ == "__main__":
    main()
