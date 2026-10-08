"""A synthetic terrain built in code: a flat source with one fBm noise layer.

Evaluates it over the source's grid, queries single points as a physics engine would, and writes
a GeoTIFF and a hillshade. The same terrain as examples/config/example_terrain_fbm.json.

    python example_terrain_fbm.py [OUTPUT_DIR]
"""

import terraingen
from _common import hillshade, output_dir, write_png


def main():
    out = output_dir()
    fbm = terraingen.NoiseLayer(terraingen.FractalParams(seed=1, wavelength=800.0, amplitude=40.0, octaves=8))
    terrain = terraingen.Terrain(terraingen.FlatSource(513, 513, 2.0), [terraingen.NoiseRelief([fbm])])

    evaluated = terraingen.evaluate(terrain)
    heights = evaluated.heights.values
    print(f"heights from {heights.min():.2f} to {heights.max():.2f} m")
    for x, y in ((0.0, 0.0), (120.5, -310.25)):
        print(f"height at ({x}, {y}): {terraingen.evaluate_at(terrain, x, y, 2.0):.3f} m")

    terraingen.write_geotiff(out / "example_terrain_fbm.tif", evaluated.heights)
    print("wrote", write_png(out / "example_terrain_fbm.png", hillshade(heights, 2.0, exaggeration=3.0)))


if __name__ == "__main__":
    main()
