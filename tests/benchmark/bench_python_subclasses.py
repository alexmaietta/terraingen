"""How much Python subclasses slow evaluation, against the built-in C++ parts they replace.

Python methods run under the GIL, so calls into them from TerrainGen's worker threads take turns:
a terrain whose hot path is in Python does not speed up with threads, and each call costs a few
microseconds. This script times the same terrain with each extension point in C++ and in Python,
on 1, 4 and 8 threads, and prints a table of milliseconds and of the slowdown against C++ on one
thread.

    PYTHONPATH=build/python python tests/benchmark/bench_python_subclasses.py [--size N] [--repeats R]
"""

import argparse
import time

import numpy as np

import terraingen

THREADS = (1, 4, 8)


class PyNoiseLayer(terraingen.Layer):
    """A sine relief, point by point."""

    def apply_at(self, height, x, y, context):
        return height + 2.0 * np.sin(x / 40.0) * np.cos(y / 40.0)


class PyArrayLayer(terraingen.Layer):
    """The same relief over the whole raster with NumPy."""

    def apply(self, heights, context):
        grid = terraingen.RasterGrid.of(heights)
        x0, step = grid.transform.origin_x, grid.transform.pixel_width
        y0 = grid.transform.origin_y
        xs = x0 + step * (np.arange(heights.width) + 0.5)
        ys = y0 - step * (np.arange(heights.height) + 0.5)
        heights.values[:] += (2.0 * np.sin(xs[None, :] / 40.0) * np.cos(ys[:, None] / 40.0)).astype(np.float32)


class PyBowl(terraingen.CraterProfile):
    """A parabolic bowl with a falling rim."""

    def height(self, distance, diameter, state):
        x = distance / (0.5 * diameter)
        depth = state.current.depth_ratio * diameter
        rim = state.current.rim_height_ratio * diameter
        return rim - depth * (1.0 - x * x) if x <= 1.0 else rim * x**-3

    def reach_radii(self):
        return 2.0


class PyFixedDegradation(terraingen.DegradationModel):
    """Every crater at a depth ratio of 0.12."""

    def state(self, crater):
        shape = terraingen.CraterMorphometry(0.12, 0.03)
        return terraingen.CraterState(shape, shape, 0.0)


class PyPowerLaw(terraingen.SizeFrequencyDistribution):
    """N(>= D) = 0.05 D^-2."""

    def __init__(self):
        super().__init__(terraingen.SfdKind.DENSITY, 1.0, 1e5)

    def cumulative_at(self, diameter):
        return 0.05 * diameter**-2.0


def crater_layer(density, profile, degradation):
    ages = terraingen.FreshAges()
    models = terraingen.CraterModels(density, ages, degradation, profile)
    options = terraingen.CraterLayerOptions(seed=1, generation=terraingen.CraterGenerationOptions(min_diameter=4.0, max_diameter=60.0))
    return terraingen.CraterLayer(models, options)


def cases():
    noise = terraingen.NoiseRelief([terraingen.NoiseLayer(terraingen.FractalParams(seed=1, wavelength=80.0, amplitude=2.0, octaves=4))])
    profile, degradation, density = terraingen.FassettThomsonProfile(), terraingen.NoDegradation(depth_ratio=0.12), terraingen.PowerLawSfd(0.05, 2.0)
    return {
        "layer, point-wise": (noise, PyNoiseLayer()),
        "layer, NumPy apply": (noise, PyArrayLayer()),
        "FunctionLayer": (noise, terraingen.FunctionLayer(lambda x, y, s, h: h + 2.0 * np.sin(x / 40.0) * np.cos(y / 40.0))),
        "crater profile": (crater_layer(density, profile, degradation), crater_layer(density, PyBowl(), degradation)),
        "degradation model": (crater_layer(density, profile, degradation), crater_layer(density, profile, PyFixedDegradation())),
        "size distribution": (crater_layer(density, profile, degradation), crater_layer(PyPowerLaw(), profile, degradation)),
    }


def time_terrain(layer, size, threads, repeats):
    terrain = terraingen.Terrain(terraingen.FlatSource(size, size, 1.0), [layer])
    terraingen.evaluate(terrain, threads=threads)
    best = float("inf")
    for _ in range(repeats):
        start = time.perf_counter()
        terraingen.evaluate(terrain, threads=threads)
        best = min(best, time.perf_counter() - start)
    return best * 1000.0


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--size", type=int, default=256)
    parser.add_argument("--repeats", type=int, default=3)
    args = parser.parse_args()
    print(f"{args.size} x {args.size} cells, best of {args.repeats}; milliseconds (slowdown against C++ on one thread)")
    header = f"{'':22s} {'':6s}" + "".join(f"{f'{t} thread' + ('s' if t > 1 else ''):>18s}" for t in THREADS)
    print(header)
    for name, (cpp, python) in cases().items():
        base = time_terrain(cpp, args.size, 1, args.repeats)
        for label, layer in (("C++", cpp), ("Python", python)):
            cells = [time_terrain(layer, args.size, t, args.repeats) for t in THREADS]
            row = "".join(f"{ms:10.1f} ({ms / base:4.1f}x)" for ms in cells)
            print(f"{name if label == 'C++' else '':22s} {label:6s}{row}")


if __name__ == "__main__":
    main()
