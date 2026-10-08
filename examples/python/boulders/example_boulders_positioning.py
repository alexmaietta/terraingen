"""Where rocks lie: patches, crater ejecta and the shape of each rock.

Rocks are not spread evenly. `RockPatches` varies their abundance from patch to patch;
`RockEjecta` adds rocks thrown out by craters large or deep enough to reach bedrock, most at the
rim and fading with the crater's age; height ratios, burial and lean shape each rock, and the
largest can be placed as 3D boulder models. Writes rock-abundance maps and a montage of hillshades,
and the boulders of the last terrain as a glTF scene.

    python example_boulders_positioning.py [OUTPUT_DIR]
"""

import pathlib
import sys

import numpy as np

import terraingen

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
from _common import hillshade, montage, normalize, output_dir, write_png

PIXEL = 0.1
SIZE = 401


def main():
    out = output_dir()
    ages = terraingen.FreshAges()
    models = terraingen.CraterModels(
        terraingen.PowerLawSfd(1e-6, 2.0), ages, terraingen.FreshnessDegradation(ages), terraingen.FassettThomsonProfile()
    )
    young = terraingen.Crater(0.0, 0.0, 20.0, age_gyr=0.01)
    craters = terraingen.CraterLayer(
        models, terraingen.CraterLayerOptions(generate=False, generation=terraingen.CraterGenerationOptions(manual=[young]))
    )
    base = dict(coverage=0.03, min_diameter=0.2, max_diameter=3.0, seed=1)
    variants = {
        "uniform": terraingen.RockLayerOptions(**base),
        "patches": terraingen.RockLayerOptions(**base, patches=terraingen.RockPatches(spread=4.0, size=12.0)),
        "crater ejecta": terraingen.RockLayerOptions(
            **base, ejecta=terraingen.RockEjecta(min_crater_diameter=5.0, coverage=0.25, reach=3.0)
        ),
        "tall, unburied, leaning boulders": terraingen.RockLayerOptions(
            **base,
            ejecta=terraingen.RockEjecta(min_crater_diameter=5.0, coverage=0.25, reach=3.0),
            min_height_ratio=0.7,
            max_height_ratio=1.0,
            min_burial=0.0,
            max_burial=0.05,
            max_lean=0.6,
            relief=terraingen.RockRelief.SHAPE,
            models=terraingen.RockModels(min_diameter=0.8),
        ),
    }
    shades, abundance = [], []
    xs = (np.arange(SIZE) - SIZE / 2 + 0.5) * PIXEL
    for name, options in variants.items():
        rocks = terraingen.RockLayer(options, craters)
        terrain = terraingen.Terrain(terraingen.FlatSource(SIZE, SIZE, PIXEL), [craters, rocks])
        evaluated = terraingen.evaluate(terrain)
        print(f"{name:34s} {len(evaluated.rocks['x']):5d} rocks, {evaluated.boulder_count} boulder models")
        shades.append(hillshade(evaluated.heights.values, PIXEL))
        grid = np.array([[sum(rocks.abundance_at(x, -y)) for x in xs[::8]] for y in xs[::8]])
        abundance.append(np.kron(normalize(grid), np.ones((8, 8)))[:SIZE, :SIZE])
    evaluated.write_boulder_models(out / "example_boulders_positioning.glb")
    print("wrote", write_png(out / "example_boulders_positioning.png", montage(shades, columns=4)))
    print("wrote", write_png(out / "example_boulders_positioning_abundance.png", montage(abundance, columns=4)))


if __name__ == "__main__":
    main()
