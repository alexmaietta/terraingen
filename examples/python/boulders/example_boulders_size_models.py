"""Rock size models: how many rocks of each size a rock layer places.

`RockLayer` draws sizes from Golombek and Rapp's (1997) model for a rock coverage by default, or from
any size-frequency distribution: the NASA SP-8023 curves for blocks between and near craters,
synthterrain's power laws, or a distribution of your own. Each is used over 0.2 to 3 m, or the part
of it the distribution covers. Prints the rocks per 100 square metres at each size and writes a
montage of hillshades.

    python example_boulders_size_models.py [OUTPUT_DIR]
"""

import pathlib
import sys

import numpy as np

import terraingen

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
from _common import hillshade, montage, output_dir, write_png

PIXEL = 0.1
SIZE = 401


def main():
    out = output_dir()
    models = {
        "Golombek-Rapp 3%": terraingen.RockLayerOptions(coverage=0.03),
        "Golombek-Rapp 10%": terraingen.RockLayerOptions(coverage=0.10),
        "SP-8023 intercrater max": terraingen.RockLayerOptions(
            distribution=terraingen.sp8023_intercrater_blocks(terraingen.RockModelCurve.MAXIMUM)
        ),
        "SP-8023 crater nominal": terraingen.RockLayerOptions(
            distribution=terraingen.sp8023_crater_blocks(terraingen.RockModelCurve.NOMINAL)
        ),
        "synthterrain Haworth": terraingen.RockLayerOptions(
            distribution=terraingen.synthterrain_rock_distribution(terraingen.SynthterrainRocks.HAWORTH)
        ),
        "power law": terraingen.RockLayerOptions(distribution=terraingen.PowerLawSfd(0.02, 2.5)),
    }
    area = (SIZE * PIXEL) ** 2
    print(f"{'':24s}" + "".join(f"{f'>= {d} m':>10s}" for d in (0.2, 0.5, 1.0)) + "   per 100 m^2")
    shades = []
    for name, options in models.items():
        low, high = options.distribution.valid_range if options.distribution else (0.0, float("inf"))
        options.min_diameter, options.max_diameter, options.seed = max(0.2, low), min(3.0, high), 1
        terrain = terraingen.Terrain(terraingen.FlatSource(SIZE, SIZE, PIXEL), [terraingen.RockLayer(options)])
        evaluated = terraingen.evaluate(terrain)
        diameters = evaluated.rocks["diameter"]
        print(f"{name:24s}" + "".join(f"{100 * np.count_nonzero(diameters >= d) / area:10.2f}" for d in (0.2, 0.5, 1.0)))
        shades.append(hillshade(evaluated.heights.values, PIXEL))
    print("wrote", write_png(out / "example_boulders_size_models.png", montage(shades, columns=3)))


if __name__ == "__main__":
    main()
