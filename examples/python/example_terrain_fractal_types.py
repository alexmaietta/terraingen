"""One terrain per fractal type, from the same seed.

fBm sums octaves; ridged turns |noise| into sharp crests; billow gives rounded forms; the hybrid
multifractal keeps lowlands smooth and highlands rough. Writes a montage of hillshades.

    python example_terrain_fractal_types.py [OUTPUT_DIR]
"""

import terraingen
from _common import hillshade, montage, output_dir, write_png

PIXEL = 2.0


def main():
    out = output_dir()
    shades = []
    for fractal in (
        terraingen.FractalType.FBM,
        terraingen.FractalType.RIDGED,
        terraingen.FractalType.BILLOW,
        terraingen.FractalType.HYBRID_MULTIFRACTAL,
    ):
        layer = terraingen.NoiseLayer(terraingen.FractalParams(seed=1, type=fractal, wavelength=400.0, amplitude=30.0))
        terrain = terraingen.Terrain(terraingen.FlatSource(257, 257, PIXEL), [terraingen.NoiseRelief([layer])])
        heights = terraingen.evaluate(terrain).heights.values
        print(f"{fractal.name:20s} mean {heights.mean():6.2f} m, std {heights.std():5.2f} m")
        shades.append(hillshade(heights, PIXEL, exaggeration=2.0))
    print("wrote", write_png(out / "example_terrain_fractal_types.png", montage(shades, columns=4)))


if __name__ == "__main__":
    main()
