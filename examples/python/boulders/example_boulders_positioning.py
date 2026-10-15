"""Where boulders lie: patches, crater ejecta and the shape of each boulder.

Boulders are not spread evenly. `BoulderPatches` varies their abundance from patch to patch;
`BoulderEjecta` adds boulders thrown out by craters large or deep enough to reach bedrock, most at the
rim and fading with the crater's age; height ratios, burial and lean shape each boulder, and the
largest can be placed as 3D boulder models. Writes boulder-abundance maps and a montage of hillshades,
and the boulder models of the last terrain as a glTF scene.

    python example_boulders_positioning.py [--viewer window|null] [--output-dir DIR] [--test]
"""

import pathlib
import sys

import numpy as np

import terraingen

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
import _common
from _common import hillshade, montage, normalize, write_png

PIXEL = 0.1
SIZE = 401


class Example(_common.Example):
    def generate(self):
        ages = terraingen.FreshAges()
        models = terraingen.CraterModels(terraingen.PowerLawSfd(1e-6, 2.0), ages, terraingen.FreshnessDegradation(ages), terraingen.FassettThomsonProfile())
        young = terraingen.Crater(0.0, 0.0, 20.0, age_gyr=0.01)
        craters = terraingen.CraterLayer(models, terraingen.CraterLayerOptions(generate=False, generation=terraingen.CraterGenerationOptions(manual=[young])))
        base = dict(coverage=0.03, min_diameter=0.2, max_diameter=3.0, seed=1)
        variants = {
            "uniform": terraingen.BoulderLayerOptions(**base),
            "patches": terraingen.BoulderLayerOptions(**base, patches=terraingen.BoulderPatches(spread=4.0, size=12.0)),
            "crater ejecta": terraingen.BoulderLayerOptions(**base, ejecta=terraingen.BoulderEjecta(min_crater_diameter=5.0, coverage=0.25, reach=3.0)),
            "tall, unburied, leaning boulders": terraingen.BoulderLayerOptions(
                **base,
                ejecta=terraingen.BoulderEjecta(min_crater_diameter=5.0, coverage=0.25, reach=3.0),
                min_height_ratio=0.7,
                max_height_ratio=1.0,
                min_burial=0.0,
                max_burial=0.05,
                max_lean=0.6,
                relief=terraingen.BoulderRelief.SHAPE,
                models=terraingen.BoulderModelThreshold(min_diameter=0.8),
            ),
        }
        self.evaluated, self.abundance = {}, []
        xs = (np.arange(SIZE) - SIZE / 2 + 0.5) * PIXEL
        for name, options in variants.items():
            boulders = terraingen.BoulderLayer(options, craters)
            terrain = terraingen.Terrain(terraingen.FlatSource(SIZE, SIZE, PIXEL), [craters, boulders])
            evaluated = terraingen.evaluate(terrain)
            print(f"{name:34s} {len(evaluated.boulders['x']):5d} boulders, {evaluated.boulder_model_count} boulder models")
            self.evaluated[name] = evaluated
            grid = np.array([[sum(boulders.abundance_at(x, -y)) for x in xs[::8]] for y in xs[::8]])
            self.abundance.append(np.kron(normalize(grid), np.ones((8, 8)))[:SIZE, :SIZE])
        self.last = evaluated
        for name in reversed(self.evaluated):
            self.show(name, self.evaluated[name].heights)

    def save(self, out):
        self.last.write_boulder_models(out / "example_boulders_positioning.glb")
        shades = [hillshade(evaluated.heights.values, PIXEL) for evaluated in self.evaluated.values()]
        print("wrote", write_png(out / "example_boulders_positioning.png", montage(shades, columns=4)))
        print("wrote", write_png(out / "example_boulders_positioning_abundance.png", montage(self.abundance, columns=4)))

    def test_final(self):
        counts = {name: len(evaluated.boulders["x"]) for name, evaluated in self.evaluated.items()}
        assert all(e.heights.shape == (SIZE, SIZE) for e in self.evaluated.values()), "the heights are 401 x 401"
        assert counts["uniform"] > 50, "more than 50 boulders without patches or ejecta"
        assert counts["crater ejecta"] > 3 * counts["uniform"], "crater ejecta add many boulders"
        assert self.evaluated["crater ejecta"].boulder_model_count == 0, "no boulder models unless asked for"
        assert 0 < self.last.boulder_model_count < counts["tall, unburied, leaning boulders"], "some boulders become boulder models"
        assert all(a.shape == (SIZE, SIZE) for a in self.abundance), "abundance maps on the heights' grid"


if __name__ == "__main__":
    _common.main(Example)
