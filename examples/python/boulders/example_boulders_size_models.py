"""Boulder size models: how many boulders of each size a boulder layer places.

`BoulderLayer` draws sizes from Golombek and Rapp's (1997) model for a boulder coverage by default, or from
any size-frequency distribution: the NASA SP-8023 curves for blocks between and near craters,
synthterrain's power laws, or a distribution of your own. Each is used over 0.2 to 3 m, or the part
of it the distribution covers. Prints the boulders per 100 square metres at each size and writes a
montage of hillshades.

    python example_boulders_size_models.py [--viewer window|null] [--output-dir DIR] [--test]
"""

import pathlib
import sys

import numpy as np

import terraingen

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
import _common
from _common import hillshade, montage, write_png

PIXEL = 0.1
SIZE = 401


class Example(_common.Example):
    def generate(self):
        models = {
            "Golombek-Rapp 3%": terraingen.BoulderLayerOptions(coverage=0.03),
            "Golombek-Rapp 10%": terraingen.BoulderLayerOptions(coverage=0.10),
            "SP-8023 intercrater max": terraingen.BoulderLayerOptions(
                distribution=terraingen.sp8023_intercrater_blocks(terraingen.SpecificationCurve.MAXIMUM)
            ),
            "SP-8023 crater nominal": terraingen.BoulderLayerOptions(
                distribution=terraingen.sp8023_crater_blocks(terraingen.SpecificationCurve.NOMINAL)
            ),
            "synthterrain Haworth": terraingen.BoulderLayerOptions(
                distribution=terraingen.synthterrain_boulder_distribution(
                    terraingen.SynthterrainBoulders.HAWORTH
                )
            ),
            "power law": terraingen.BoulderLayerOptions(distribution=terraingen.PowerLawSfd(0.02, 2.5)),
        }
        area = (SIZE * PIXEL) ** 2
        print(f"{'':24s}" + "".join(f"{f'>= {d} m':>10s}" for d in (0.2, 0.5, 1.0)) + "   per 100 m^2")
        self.heights, self.diameters, self.ranges = {}, {}, {}
        for name, options in models.items():
            low, high = options.distribution.valid_range if options.distribution else (0.0, float("inf"))
            options.min_diameter_m, options.max_diameter_m, options.seed = max(0.2, low), min(3.0, high), 1
            terrain = terraingen.TerrainGenerator(
                terraingen.FlatSource(SIZE, SIZE, PIXEL), [terraingen.BoulderLayer(options)]
            )
            self.heights[name] = terraingen.evaluate(terrain)
            diameters = terraingen.placed_boulders(terrain).table["diameter"]
            print(
                f"{name:24s}"
                + "".join(f"{100 * np.count_nonzero(diameters >= d) / area:10.2f}" for d in (0.2, 0.5, 1.0))
            )
            self.diameters[name] = diameters
            self.ranges[name] = (options.min_diameter_m, options.max_diameter_m)
            self.show(name, self.heights[name])

    def save(self, out):
        shades = [hillshade(heights.values, PIXEL) for heights in self.heights.values()]
        print("wrote", write_png(out / "example_boulders_size_models.png", montage(shades, columns=3)))

    def test_final(self):
        counts = {name: len(diameters) for name, diameters in self.diameters.items()}
        assert len(counts) == 6 and all(h.shape == (SIZE, SIZE) for h in self.heights.values()), (
            "six 401 x 401 terrains"
        )
        assert all(count > 0 for count in counts.values()), "every model places boulders"
        assert counts["Golombek-Rapp 10%"] > 2 * counts["Golombek-Rapp 3%"], (
            "more coverage places more boulders"
        )
        for name, diameters in self.diameters.items():
            low, high = self.ranges[name]
            assert diameters.min() >= low - 1e-9 and diameters.max() <= high + 1e-9, (
                f"{name} boulders lie in its size range"
            )
            assert np.count_nonzero(diameters >= 1.0) <= np.count_nonzero(diameters >= 0.5), (
                f"{name} has fewer large boulders than small"
            )


if __name__ == "__main__":
    _common.main(Example)
