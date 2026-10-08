"""Hand-placed craters on a synthetic terrain, with no generated population.

Each crater has a position, diameter and age, and its age sets how degraded it is. The same
terrain as examples/config/example_craters_manual.json.

    python example_craters_manual.py [OUTPUT_DIR]
"""

import terraingen
from _common import hillshade, output_dir, write_png


def main():
    out = output_dir()
    fbm = terraingen.NoiseLayer(terraingen.FractalParams(seed=1, wavelength=800.0, amplitude=40.0))
    ages = terraingen.UniformAges(3.5)
    models = terraingen.CraterModels(
        density=terraingen.PowerLawSfd(1e-6, 2.0),
        ages=ages,
        degradation=terraingen.FreshnessDegradation(ages),
        profile=terraingen.FassettThomsonProfile(),
    )
    manual = [
        terraingen.Crater(0.0, 0.0, 200.0, age_gyr=0.5),
        terraingen.Crater(-250.0, 180.0, 90.0, age_gyr=3.0),
        terraingen.Crater(260.0, -200.0, 120.0, age_gyr=1.5),
        terraingen.Crater(150.0, 250.0, 40.0, age_gyr=0.1),
    ]
    craters = terraingen.CraterLayer(models, terraingen.CraterLayerOptions(generate=False, generation=terraingen.CraterGenerationOptions(manual=manual)))
    terrain = terraingen.Terrain(terraingen.FlatSource(513, 513, 2.0), [terraingen.NoiseRelief([fbm]), craters])

    evaluated = terraingen.evaluate(terrain)
    table = evaluated.craters
    for diameter, age, ratio, freshness in zip(table["diameter"], table["age_gyr"], table["depth_ratio"], table["freshness"]):
        print(f"{diameter:4.0f} m crater, {age:3.1f} Gyr old: depth/diameter {ratio:.3f}, freshness {freshness:.2f}")
    terraingen.write_geotiff(out / "example_craters_manual.tif", evaluated.heights)
    print("wrote", write_png(out / "example_craters_manual.png", hillshade(evaluated.heights.values, 2.0)))


if __name__ == "__main__":
    main()
