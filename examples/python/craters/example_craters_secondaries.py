"""Secondary craters around a young primary.

Ejecta from a primary crater makes secondary craters beyond its rim: their largest size at each
range follows Singer et al. (2020), their density falls off with range (Chapman 1968), and they
extend to about Elliott et al.'s (2018) longest rays. How many there are must be given
(`onset_count`). Prints the secondaries by range and writes the heights.

    python example_craters_secondaries.py [--viewer window|null] [--output-dir DIR] [--test]
"""

import pathlib
import sys

import numpy as np

import terraingen

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
import _common
from _common import hillshade, write_png

PIXEL = 12.0


class Example(_common.Example):
    def generate(self):
        primary = terraingen.Crater(0.0, 0.0, 2000.0, age_gyr=0.05)
        ages = terraingen.FreshAges()
        models = terraingen.CraterModels(
            terraingen.PowerLawSfd(1e-6, 2.0),
            ages,
            terraingen.NoDegradation(terraingen.lunar_fresh_crater_morphometry),
            terraingen.FassettThomsonProfile(),
        )
        primaries = terraingen.CraterLayer(
            models,
            terraingen.CraterLayerOptions(
                generate_random=False, generation=terraingen.CraterGenerationOptions(manual=[primary])
            ),
        )
        options = terraingen.SecondaryCraterOptions(seed=4, onset_count=20.0, min_diameter_m=30.0)
        secondaries = terraingen.SecondaryCraterLayer(
            primaries,
            terraingen.FassettThomsonProfile(),
            terraingen.NoDegradation(terraingen.lunar_fresh_crater_morphometry, depth_ratio=0.11),
            options,
        )
        self.onset = terraingen.secondary_onset_range(primary.diameter)
        self.outer = secondaries.outer_range(primary.diameter)
        print(f"secondaries from {self.onset / 1000:.1f} to {self.outer / 1000:.1f} km from the centre")
        self.table = secondaries.secondaries((-self.outer, -self.outer, self.outer, self.outer))
        ranges = np.hypot(self.table["x"], self.table["y"])
        self.bands = []
        for start in np.arange(self.onset, self.outer, 2000.0):
            inside = (ranges >= start) & (ranges < start + 2000.0)
            largest = self.table["diameter"][inside].max() if inside.any() else 0.0
            limit = terraingen.singer_max_secondary_diameter(primary.diameter, start)
            self.bands.append((inside.sum(), largest, limit))
            print(
                f"  {start / 1000:5.1f} km: {inside.sum():3d} secondaries, largest {largest:5.1f} m (Singer limit {limit:5.1f} m)"
            )

        terrain = terraingen.TerrainGenerator(
            terraingen.FlatSource(1537, 1537, PIXEL), [primaries, secondaries]
        )
        self.heights = terraingen.evaluate(terrain)
        self.show("heights", self.heights)

    def save(self, out):
        terraingen.write_geotiff(out / "example_craters_secondaries.tif", self.heights)
        print(
            "wrote",
            write_png(out / "example_craters_secondaries.png", hillshade(self.heights.values, PIXEL)),
        )

    def test_final(self):
        ranges = np.hypot(self.table["x"], self.table["y"])
        assert self.heights.shape == (1537, 1537), "the heights are 1537 x 1537"
        assert np.all(np.isfinite(self.heights.values)), "every height is finite"
        assert len(ranges) > 10, "more than 10 secondaries"
        assert self.table["diameter"].min() >= 30.0, "every secondary is 30 m or more"
        assert ranges.min() >= 0.99 * self.onset and ranges.max() <= 1.01 * self.outer, (
            "the secondaries lie between the onset and outer ranges"
        )
        assert all(largest <= 1.01 * limit for _, largest, limit in self.bands), (
            "no secondary exceeds Singer's largest size for its range"
        )


if __name__ == "__main__":
    _common.main(Example)
