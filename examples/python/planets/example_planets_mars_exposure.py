"""Mars: sand where regolith collects, boulders where bedrock is exposed.

An exposure field estimates how exposed the ground is, from 0 (deep cover in hollows) to 1 (bare
crests and steep slopes). Bedforms with `exposure` set grow only on covered ground, and boulders with
`exposure` set gather on exposed ground (up to `bedrock_abundance` times more). Prints how much of
each lies on exposed ground and writes the heights, the exposure map and a hillshade.

    python example_planets_mars_exposure.py [--viewer window|null] [--output-dir DIR] [--test]
"""

import pathlib
import sys

import numpy as np

import terraingen

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
import _common
from _common import hillshade, montage, write_png

PIXEL = 0.25
SIZE = 513


class Example(_common.Example):
    def generate(self):
        relief = terraingen.NoiseRelief(
            [
                terraingen.NoiseLayer(
                    terraingen.FractalParams(seed=3, wavelength=60.0, amplitude=5.0, octaves=7)
                )
            ]
        )
        exposure = terraingen.ExposureParams(reach=8.0)
        sand = terraingen.BedformLayer(
            terraingen.BedformOptions(
                wavelength=2.5,
                height=0.25,
                wind_azimuth=60.0,
                cover=0.8,
                field_size=30.0,
                seed=3,
                exposure=exposure,
            )
        )
        boulders = terraingen.BoulderLayer(
            terraingen.BoulderLayerOptions(
                seed=3,
                coverage=0.02,
                min_diameter=0.3,
                max_diameter=3.0,
                exposure=exposure,
                bedrock_abundance=4.0,
            )
        )
        source = terraingen.FlatSource(SIZE, SIZE, PIXEL)
        terrain = terraingen.Terrain(source, [relief, sand, boulders])
        grid = source.grid()
        self.exposure_map = (
            terraingen.ExposureField(exposure)
            .fill(terraingen.Terrain(source, [relief]), 1, grid, PIXEL)
            .values
        )
        self.heights = terraingen.evaluate(terrain)
        self.boulders = terraingen.placed_boulders(terrain).table

        self.covered = self.exposure_map < 0.5
        xs = (np.arange(SIZE) - SIZE / 2 + 0.5) * PIXEL
        ground = terraingen.Terrain(source, [relief])
        self.sandy = np.array([[sand.cover_at(x, -y, ground, 1) > 0 for x in xs[::8]] for y in xs[::8]])
        rows = np.clip(((grid.transform.origin_y - self.boulders["y"]) / PIXEL).astype(int), 0, SIZE - 1)
        cols = np.clip(((self.boulders["x"] - grid.transform.origin_x) / PIXEL).astype(int), 0, SIZE - 1)
        self.boulders_exposed = np.mean(~self.covered[rows, cols])
        print(f"{self.covered.mean():.0%} of the ground is mostly covered")
        print(f"bedform fields cover {self.sandy.mean():.0%} of the area")
        print(f"{len(cols)} boulders, {self.boulders_exposed:.0%} of them on exposed ground")
        self.show("heights", self.heights)
        self.show("exposure", self.heights, albedo=self.exposure_map)

    def save(self, out):
        terraingen.write_geotiff(out / "example_planets_mars_exposure.tif", self.heights)
        images = [self.exposure_map, hillshade(self.heights.values, PIXEL)]
        print("wrote", write_png(out / "example_planets_mars_exposure.png", montage(images, columns=2)))

    def test_final(self):
        heights = self.heights.values
        assert heights.shape == (SIZE, SIZE) and np.isfinite(heights).all(), "finite 513 x 513 heights"
        assert (
            self.exposure_map.shape == (SIZE, SIZE)
            and 0.0 <= self.exposure_map.min()
            and self.exposure_map.max() <= 1.0
        ), "exposure lies in [0, 1]"
        assert 0.3 < self.covered.mean() < 0.95, "most, but not all, of the ground is covered"
        assert 0.2 < self.sandy.mean() < 0.95, "bedform fields cover part of the area"
        assert len(self.boulders["x"]) > 20, "more than 20 boulders"
        assert self.boulders_exposed > 1.0 - self.covered.mean(), (
            "boulders gather on exposed ground, more than its share of the area"
        )


if __name__ == "__main__":
    _common.main(Example)
