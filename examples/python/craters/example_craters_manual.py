"""Hand-placed craters on a synthetic terrain, with no generated population.

Each crater has a position, diameter and age, and its age sets how degraded it is. The same
terrain as examples/config/craters/example_craters_manual.json.

    python example_craters_manual.py [--viewer window|null] [--output-dir DIR] [--test]
"""

import pathlib
import sys

import numpy as np

import terraingen

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
import _common
from _common import hillshade, write_png


class Example(_common.Example):
    def generate(self):
        fbm = terraingen.NoiseLayer(terraingen.FractalParams(seed=1, wavelength=800.0, amplitude=40.0))
        ages = terraingen.UniformAges(3.5)
        models = terraingen.CraterModels(
            density=terraingen.PowerLawSfd(1e-6, 2.0),
            ages=ages,
            degradation=terraingen.FreshnessDegradation(ages, terraingen.lunar_fresh_crater_morphometry),
            profile=terraingen.FassettThomsonProfile(),
        )
        manual = [
            terraingen.Crater(0.0, 0.0, 200.0, age_gyr=0.5),
            terraingen.Crater(-250.0, 180.0, 90.0, age_gyr=3.0),
            terraingen.Crater(260.0, -200.0, 120.0, age_gyr=1.5),
            terraingen.Crater(150.0, 250.0, 40.0, age_gyr=0.1),
        ]
        craters = terraingen.CraterLayer(
            models,
            terraingen.CraterLayerOptions(
                generate=False, generation=terraingen.CraterGenerationOptions(manual=manual)
            ),
        )
        terrain = terraingen.Terrain(
            terraingen.FlatSource(513, 513, 2.0), [terraingen.NoiseRelief([fbm]), craters]
        )

        self.evaluated = terraingen.evaluate(terrain)
        table = self.evaluated.craters
        for diameter, age, ratio, freshness in zip(
            table["diameter"], table["age_gyr"], table["depth_ratio"], table["freshness"], strict=True
        ):
            print(
                f"{diameter:4.0f} m crater, {age:3.1f} Gyr old: depth/diameter {ratio:.3f}, freshness {freshness:.2f}"
            )
        self.show("heights", self.evaluated.heights)

    def save(self, out):
        terraingen.write_geotiff(out / "example_craters_manual.tif", self.evaluated.heights)
        print(
            "wrote",
            write_png(out / "example_craters_manual.png", hillshade(self.evaluated.heights.values, 2.0)),
        )

    def test_final(self):
        table = self.evaluated.craters
        assert self.evaluated.heights.shape == (513, 513), "the heights are 513 x 513"
        assert np.all(np.isfinite(self.evaluated.heights.values)), "every height is finite"
        assert sorted(table["diameter"]) == [40.0, 90.0, 120.0, 200.0], "exactly the four hand-placed craters"
        order = np.argsort(table["age_gyr"])
        assert np.all(np.diff(table["freshness"][order]) <= 0.0), "older craters are less fresh"
        assert table["depth_ratio"][order[-1]] == table["depth_ratio"].min(), (
            "the oldest crater is the shallowest"
        )


if __name__ == "__main__":
    _common.main(Example)
