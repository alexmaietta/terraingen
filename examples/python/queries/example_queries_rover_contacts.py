"""A rover's wheel contacts from point queries, as a physics engine asks for them each step.

Physics engines (Chrono, for example) query the ground under each wheel every step rather than a
whole grid. `height_at(terrain, x, y, spacing)` answers one point, equal to the grid cell centred
there, and releases the GIL, so several threads can query at once. This example drives a rover with
four wheels across cratered, rocky ground, takes each wheel's height, and from them the body's
height, pitch and roll; then compares one thread with four.

    python example_queries_rover_contacts.py [--viewer window|null] [--output-dir DIR] [--test]
"""

import math
import pathlib
import sys
import time
from concurrent.futures import ThreadPoolExecutor

import numpy as np

import terraingen

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
import _common
from _common import colour_name, line_plot, write_png

SPACING = 0.05
WHEELS = [(1.0, 0.8), (1.0, -0.8), (-1.0, 0.8), (-1.0, -0.8)]
STEPS = 400


def build_terrain():
    """Cratered, rocky ground with gentle fBm relief."""
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
    generation = terraingen.CraterGenerationOptions(d_min=2.5, d_max=40.0)
    craters = terraingen.CraterLayer(models, terraingen.CraterLayerOptions(seed=6, generation=generation))
    boulders = terraingen.BoulderLayer(
        terraingen.BoulderLayerOptions(seed=6, coverage=0.06, min_diameter_m=0.1, max_diameter_m=1.0), craters
    )
    noise = terraingen.NoiseRelief(
        [terraingen.NoiseLayer(terraingen.FractalParams(seed=6, wavelength=50.0, amplitude=1.0))]
    )
    return terraingen.TerrainGenerator(terraingen.FlatSource(64, 64, 1.0), [noise, craters, boulders])


def wheel_points(x, y, heading):
    """The four wheels' contact points of a rover at (x, y) heading `heading` radians."""
    c, s = math.cos(heading), math.sin(heading)
    return [(x + c * dx - s * dy, y + s * dx + c * dy) for dx, dy in WHEELS]


class Example(_common.Example):
    def generate(self):
        terrain = build_terrain()
        path = [
            (-20.0 + 0.1 * i, 2.0 * math.sin(0.02 * i), math.atan(0.04 * math.cos(0.02 * i)))
            for i in range(STEPS)
        ]

        def contacts(step):
            x, y, heading = step
            return [terraingen.height_at(terrain, px, py, SPACING) for px, py in wheel_points(x, y, heading)]

        self.runs = {}
        for threads in (1, 4):
            start = time.perf_counter()
            with ThreadPoolExecutor(max_workers=threads) as pool:
                self.runs[threads] = np.array(list(pool.map(contacts, path)))
            print(
                f"{threads} thread(s): {4 * STEPS} wheel queries in {(time.perf_counter() - start) * 1000:.0f} ms"
            )

        heights = self.runs[4]
        self.body = heights.mean(axis=1)
        self.pitch = np.degrees(np.arctan2(heights[:, :2].mean(axis=1) - heights[:, 2:].mean(axis=1), 2.0))
        self.roll = np.degrees(
            np.arctan2(heights[:, [0, 2]].mean(axis=1) - heights[:, [1, 3]].mean(axis=1), 1.6)
        )
        print(
            f"body height {self.body.min():.2f} to {self.body.max():.2f} m; pitch up to {np.abs(self.pitch).max():.1f} deg, roll up to {np.abs(self.roll).max():.1f} deg"
        )
        print(
            "legend:",
            ", ".join(
                f"{colour_name(i)} {n}"
                for i, n in enumerate(("body height (m)", "pitch (deg)", "roll (deg)"))
            ),
        )
        self.ground = terraingen.evaluate(terrain, grid=terraingen.RasterGrid.centred(192, 48, 0.25))
        self.show("ground along the path", self.ground)

    def save(self, out):
        distance = 0.1 * np.arange(STEPS)
        image = line_plot([(distance, self.body), (distance, self.pitch), (distance, self.roll)])
        print("wrote", write_png(out / "example_queries_rover_contacts.png", image))

    def test_final(self):
        assert self.runs[1].shape == (STEPS, 4), "four wheel heights at every step"
        assert np.array_equal(self.runs[1], self.runs[4]), "four threads give the same contacts as one"
        assert np.isfinite(self.runs[1]).all(), "every contact height is finite"
        assert -20.0 < self.body.min() < self.body.max() < 20.0, (
            "the body's height varies within 20 m of the datum"
        )
        assert 0.0 < np.abs(self.pitch).max() < 60.0 and 0.0 < np.abs(self.roll).max() < 60.0, (
            "the body pitches and rolls, by less than 60 degrees"
        )
        assert self.ground.shape == (48, 192), "the ground strip is 192 x 48"


if __name__ == "__main__":
    _common.main(Example)
