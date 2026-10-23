"""Point queries: the height at any point, as a physics engine or rover simulation asks for it.

`height_at(terrain, x, y, spacing)` gives the height at one point sampled every `spacing`
meters, equal to the cell centered there of a grid evaluated at that spacing, without evaluating a
grid. This example checks that against a grid, samples a rover's path across craters, and times
the queries.

    python example_queries_points.py [--viewer window|null] [--output-dir DIR] [--test]
"""

import pathlib
import sys
import time

import numpy as np

import terraingen

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
import _common
from _common import line_plot, write_png

SPACING = 0.5


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
        craters = terraingen.CraterLayer(
            models,
            terraingen.CraterLayerOptions(
                generation=terraingen.CraterGenerationOptions(d_min=3.0, d_max=60.0)
            ),
        )
        noise = terraingen.NoiseRelief(
            [terraingen.NoiseLayer(terraingen.FractalParams(seed=3, wavelength=100.0, amplitude=2.0))]
        )
        terrain = terraingen.TerrainGenerator(terraingen.FlatSource(64, 64, SPACING), [noise, craters])

        self.grid = terraingen.evaluate(terrain, spacing=SPACING)
        self.center = self.grid.values[32, 32]
        self.query = terraingen.height_at(terrain, 0.25, -0.25, SPACING)
        print(f"grid cell at the center {self.center:.4f} m, point query {self.query:.4f} m")

        self.path = np.linspace(-100.0, 100.0, 801)
        start = time.perf_counter()
        self.heights = np.array([terraingen.height_at(terrain, x, 0.3 * x, SPACING) for x in self.path])
        elapsed = time.perf_counter() - start
        print(
            f"{len(self.path)} queries along a 210 m path in {elapsed * 1000:.0f} ms ({len(self.path) / elapsed:.0f} per second)"
        )
        self.slopes = np.degrees(
            np.arctan(np.abs(np.diff(self.heights)) / (np.hypot(1.0, 0.3) * np.diff(self.path)))
        )
        print(
            f"heights {self.heights.min():.2f} to {self.heights.max():.2f} m, steepest slope along the path {self.slopes.max():.1f} degrees"
        )
        self.show("grid", self.grid)

    def save(self, out):
        print(
            "wrote",
            write_png(out / "example_queries_points.png", line_plot([(self.path, self.heights)])),
        )

    def test_final(self):
        assert self.grid.shape == (64, 64), "the grid is 64 x 64"
        assert abs(self.query - self.center) < 1e-4, "the point query equals the grid cell centered there"
        assert self.heights.shape == (801,) and np.isfinite(self.heights).all(), (
            "801 finite heights along the path"
        )
        assert -20.0 < self.heights.min() < self.heights.max() < 20.0, (
            "path heights vary within 20 m of the datum"
        )
        assert self.slopes.max() < 80.0, "no slope along the path is near vertical"


if __name__ == "__main__":
    _common.main(Example)
