"""Point queries: the height at any point, as a physics engine or rover simulation asks for it.

`evaluate_at(terrain, x, y, spacing)` gives the height at one point sampled every `spacing`
metres, equal to the cell centred there of a grid evaluated at that spacing, without evaluating a
grid. This example checks that against a grid, samples a rover's path across craters, and times
the queries.

    python example_tiles_point_queries.py [OUTPUT_DIR]
"""

import pathlib
import sys
import time

import numpy as np

import terraingen

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
from _common import line_plot, output_dir, write_png

SPACING = 0.5


def main():
    out = output_dir()
    surface = terraingen.lunar_surface(3.5)
    models = terraingen.CraterModels(
        surface.density, surface.ages, terraingen.FreshnessDegradation(surface.ages), terraingen.FassettThomsonProfile()
    )
    craters = terraingen.CraterLayer(
        models, terraingen.CraterLayerOptions(generation=terraingen.CraterGenerationOptions(min_diameter=3.0, max_diameter=60.0))
    )
    noise = terraingen.NoiseRelief([terraingen.NoiseLayer(terraingen.FractalParams(seed=3, wavelength=100.0, amplitude=2.0))])
    terrain = terraingen.Terrain(terraingen.FlatSource(64, 64, SPACING), [noise, craters])

    grid = terraingen.evaluate(terrain, spacing=SPACING).heights
    centre = grid.values[32, 32]
    print(f"grid cell at the centre {centre:.4f} m, point query {terraingen.evaluate_at(terrain, 0.25, -0.25, SPACING):.4f} m")

    path = np.linspace(-100.0, 100.0, 801)
    start = time.perf_counter()
    heights = np.array([terraingen.evaluate_at(terrain, x, 0.3 * x, SPACING) for x in path])
    elapsed = time.perf_counter() - start
    print(f"{len(path)} queries along a 210 m path in {elapsed * 1000:.0f} ms ({len(path) / elapsed:.0f} per second)")
    slopes = np.degrees(np.arctan(np.abs(np.diff(heights)) / (np.hypot(1.0, 0.3) * np.diff(path))))
    print(f"heights {heights.min():.2f} to {heights.max():.2f} m, steepest slope along the path {slopes.max():.1f} degrees")
    print("wrote", write_png(out / "example_tiles_point_queries.png", line_plot([(path, heights)])))


if __name__ == "__main__":
    main()
