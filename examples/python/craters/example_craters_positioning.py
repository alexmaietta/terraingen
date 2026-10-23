"""Where craters go: generated positions, spacing and a cap on their number.

Crater positions are drawn per cell with counter-based random numbers, so any region gives the same
craters as any larger one. `CraterLayer.craters(region)` lists them without evaluating heights.
`CraterSpacing` keeps craters of similar size apart (no two within `separation` larger diameters
when their sizes differ by at most `diameter_ratio`; off by default), and `max_craters` keeps only
that many.
Prints counts and nearest-neighbor distances and writes a montage of hillshades.

    python example_craters_positioning.py [--viewer window|null] [--output-dir DIR] [--test]
"""

import pathlib
import sys

import numpy as np

import terraingen

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
import _common
from _common import hillshade, montage, write_png

REGION = (-256.0, -256.0, 256.0, 256.0)


def nearest_neighbor(x, y):
    d = np.hypot(x[:, None] - x[None, :], y[:, None] - y[None, :])
    np.fill_diagonal(d, np.inf)
    return d.min(axis=1)


class Example(_common.Example):
    def generate(self):
        production = terraingen.extended_neukum_production_function()
        equilibrium = terraingen.trask_equilibrium()
        density = terraingen.MinimumSfd(
            [terraingen.ProductionSurface(production, 3.5, terraingen.neukum_lunar_chronology), equilibrium]
        )
        ages = terraingen.SteadyStateAges(production, equilibrium, 3.5, terraingen.neukum_lunar_chronology)
        models = terraingen.CraterModels(
            density,
            ages,
            terraingen.FreshnessDegradation(ages, terraingen.lunar_fresh_crater_morphometry),
            terraingen.FassettThomsonProfile(),
        )
        settings = {
            "no spacing": terraingen.CraterGenerationOptions(
                spacing=terraingen.CraterSpacing(separation=0.0)
            ),
            "some spacing": terraingen.CraterGenerationOptions(
                spacing=terraingen.CraterSpacing(diameter_ratio=1.5, separation=0.5)
            ),
            "wide spacing": terraingen.CraterGenerationOptions(
                spacing=terraingen.CraterSpacing(diameter_ratio=3.0, separation=1.5)
            ),
            "at most 100": terraingen.CraterGenerationOptions(max_craters=100),
        }
        self.counts = {}
        self.median_gaps = {}
        self.heights = {}
        for name, generation in settings.items():
            generation.d_min, generation.d_max = 6.0, 120.0
            layer = terraingen.CraterLayer(
                models, terraingen.CraterLayerOptions(seed=8, generation=generation)
            )
            table = layer.craters(REGION)
            gaps = nearest_neighbor(table["x"], table["y"]) / table["diameter"]
            self.counts[name] = len(table["x"])
            self.median_gaps[name] = np.median(gaps)
            print(
                f"{name:16s} {len(table['x']):5d} craters, median nearest neighbor {np.median(gaps):.2f} diameters"
            )
            heights = terraingen.evaluate(
                terraingen.TerrainGenerator(terraingen.FlatSource(513, 513, 1.0), [layer])
            )
            self.heights[name] = heights.values
            self.show(name, heights)

    def save(self, out):
        shades = [hillshade(heights, 1.0) for heights in self.heights.values()]
        print("wrote", write_png(out / "example_craters_positioning.png", montage(shades, columns=4)))

    def test_final(self):
        counts, gaps = self.counts, self.median_gaps
        assert all(
            heights.shape == (513, 513) and np.all(np.isfinite(heights)) for heights in self.heights.values()
        ), "every terrain is 513 x 513 and finite"
        assert counts["no spacing"] > 100, "more than 100 craters without spacing"
        assert counts["no spacing"] >= counts["some spacing"] >= counts["wide spacing"], (
            "wider spacing keeps fewer craters"
        )
        assert gaps["wide spacing"] > gaps["no spacing"], "wider spacing moves neighbors apart"
        assert counts["at most 100"] <= 100, "max_craters caps the count"


if __name__ == "__main__":
    _common.main(Example)
