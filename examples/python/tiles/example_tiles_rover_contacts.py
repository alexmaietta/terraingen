"""A rover's wheel contacts from point queries, as a physics engine asks for them each step.

Physics engines (Chrono, for example) query the ground under each wheel every step rather than a
whole grid. `evaluate_at(terrain, x, y, spacing)` answers one point, equal to the grid cell centred
there, and releases the GIL, so several threads can query at once. This example drives a rover with
four wheels across cratered, rocky ground, takes each wheel's height, and from them the body's
height, pitch and roll; then compares one thread with four.

    python example_tiles_rover_contacts.py [OUTPUT_DIR]
"""

import math
import pathlib
import sys
import time
from concurrent.futures import ThreadPoolExecutor

import numpy as np

import terraingen

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
from _common import colour_name, line_plot, output_dir, write_png

SPACING = 0.05
WHEELS = [(1.0, 0.8), (1.0, -0.8), (-1.0, 0.8), (-1.0, -0.8)]


def build_terrain():
    surface = terraingen.lunar_surface(3.5)
    models = terraingen.CraterModels(
        surface.density, surface.ages, terraingen.FreshnessDegradation(surface.ages), terraingen.FassettThomsonProfile()
    )
    generation = terraingen.CraterGenerationOptions(min_diameter=2.5, max_diameter=40.0)
    craters = terraingen.CraterLayer(models, terraingen.CraterLayerOptions(seed=6, generation=generation))
    rocks = terraingen.RockLayer(
        terraingen.RockLayerOptions(seed=6, coverage=0.06, min_diameter=0.1, max_diameter=1.0), craters
    )
    noise = terraingen.NoiseRelief([terraingen.NoiseLayer(terraingen.FractalParams(seed=6, wavelength=50.0, amplitude=1.0))])
    return terraingen.Terrain(terraingen.FlatSource(64, 64, 1.0), [noise, craters, rocks])


def wheel_points(x, y, heading):
    c, s = math.cos(heading), math.sin(heading)
    return [(x + c * dx - s * dy, y + s * dx + c * dy) for dx, dy in WHEELS]


def main():
    out = output_dir()
    terrain = build_terrain()
    steps = 400
    path = [(-20.0 + 0.1 * i, 2.0 * math.sin(0.02 * i), math.atan(0.04 * math.cos(0.02 * i))) for i in range(steps)]

    def contacts(step):
        x, y, heading = step
        return [terraingen.evaluate_at(terrain, px, py, SPACING) for px, py in wheel_points(x, y, heading)]

    timings = {}
    for threads in (1, 4):
        start = time.perf_counter()
        with ThreadPoolExecutor(max_workers=threads) as pool:
            heights = np.array(list(pool.map(contacts, path)))
        timings[threads] = time.perf_counter() - start
        print(f"{threads} thread(s): {4 * steps} wheel queries in {timings[threads] * 1000:.0f} ms")

    body = heights.mean(axis=1)
    pitch = np.degrees(np.arctan2(heights[:, :2].mean(axis=1) - heights[:, 2:].mean(axis=1), 2.0))
    roll = np.degrees(np.arctan2(heights[:, [0, 2]].mean(axis=1) - heights[:, [1, 3]].mean(axis=1), 1.6))
    print(f"body height {body.min():.2f} to {body.max():.2f} m; pitch up to {np.abs(pitch).max():.1f} deg, roll up to {np.abs(roll).max():.1f} deg")
    distance = 0.1 * np.arange(steps)
    print("legend:", ", ".join(f"{colour_name(i)} {n}" for i, n in enumerate(("body height (m)", "pitch (deg)", "roll (deg)"))))
    image = line_plot([(distance, body), (distance, pitch), (distance, roll)])
    print("wrote", write_png(out / "example_tiles_rover_contacts.png", image))


if __name__ == "__main__":
    main()
