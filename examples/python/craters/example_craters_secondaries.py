"""Secondary craters around a young primary.

Ejecta from a primary crater makes secondary craters beyond its rim: their largest size at each
range follows Singer et al. (2020), their density falls off with range (Chapman 1968), and they
extend to about Elliott et al.'s (2018) longest rays. How many there are must be given
(`onset_count`). Prints the secondaries by range and writes the heights.

    python example_craters_secondaries.py [OUTPUT_DIR]
"""

import pathlib
import sys

import numpy as np

import terraingen

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
from _common import hillshade, output_dir, write_png

PIXEL = 12.0


def main():
    out = output_dir()
    primary = terraingen.Crater(0.0, 0.0, 2000.0, age_gyr=0.05)
    ages = terraingen.FreshAges()
    models = terraingen.CraterModels(
        terraingen.PowerLawSfd(1e-6, 2.0), ages, terraingen.NoDegradation(), terraingen.FassettThomsonProfile()
    )
    primaries = terraingen.CraterLayer(
        models, terraingen.CraterLayerOptions(generate=False, generation=terraingen.CraterGenerationOptions(manual=[primary]))
    )
    options = terraingen.SecondaryCraterOptions(seed=4, onset_count=20.0, min_diameter=30.0)
    secondaries = terraingen.SecondaryCraterLayer(
        primaries, terraingen.FassettThomsonProfile(), terraingen.NoDegradation(depth_ratio=0.11), options
    )
    onset = terraingen.secondary_onset_range(primary.diameter)
    outer = secondaries.outer_range(primary.diameter)
    print(f"secondaries from {onset / 1000:.1f} to {outer / 1000:.1f} km from the centre")
    table = secondaries.secondaries((-outer, -outer, outer, outer))
    ranges = np.hypot(table["x"], table["y"])
    for start in np.arange(onset, outer, 2000.0):
        inside = (ranges >= start) & (ranges < start + 2000.0)
        largest = table["diameter"][inside].max() if inside.any() else 0.0
        limit = terraingen.singer_max_secondary_diameter(primary.diameter, start)
        print(f"  {start / 1000:5.1f} km: {inside.sum():3d} secondaries, largest {largest:5.1f} m (Singer limit {limit:5.1f} m)")

    terrain = terraingen.Terrain(terraingen.FlatSource(1537, 1537, PIXEL), [primaries, secondaries])
    evaluated = terraingen.evaluate(terrain)
    terraingen.write_geotiff(out / "example_craters_secondaries.tif", evaluated.heights)
    print("wrote", write_png(out / "example_craters_secondaries.png", hillshade(evaluated.heights.values, PIXEL)))


if __name__ == "__main__":
    main()
