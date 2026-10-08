"""Irregular and elongated crater outlines.

Real craters are not perfect circles: `CraterIrregularity` roughens rims with random harmonics (a
power-law spectrum or the fits of Du et al. 2024) and stretches a share of craters into ellipses,
optionally along a common heading. Writes a montage: round craters, rough rims, elongated craters
and both together.

    python example_craters_irregularity.py [OUTPUT_DIR]
"""

import terraingen
from _common import hillshade, montage, output_dir, write_png

PIXEL = 1.0


def main():
    out = output_dir()
    surface = terraingen.lunar_surface(3.5)
    models = terraingen.CraterModels(
        surface.density, surface.ages, terraingen.FreshnessDegradation(surface.ages), terraingen.FassettThomsonProfile()
    )
    uniform = terraingen.ValueDistributionKind.UNIFORM
    variants = {
        "round": terraingen.CraterIrregularity(),
        "rough rims": terraingen.CraterIrregularity(
            enabled=True, seed=3, elongated_share=0.0, roughness=terraingen.ValueDistribution(uniform, 0.03, 0.06)
        ),
        "elongated": terraingen.CraterIrregularity(
            enabled=True,
            seed=3,
            roughness=terraingen.ValueDistribution(a=0.0),
            elongated_share=0.6,
            ellipticity=terraingen.ValueDistribution(uniform, 1.3, 1.8),
            heading=0.5,
            heading_spread=0.2,
        ),
        "both": terraingen.CraterIrregularity(
            enabled=True, seed=3, roughness=terraingen.ValueDistribution(uniform, 0.02, 0.05), elongated_share=0.3
        ),
    }
    generation = terraingen.CraterGenerationOptions(min_diameter=15.0, max_diameter=150.0)
    shades = []
    for name, irregularity in variants.items():
        apply = terraingen.CraterApplyOptions(irregularity=irregularity)
        layer = terraingen.CraterLayer(models, terraingen.CraterLayerOptions(seed=4, generation=generation, apply=apply))
        heights = terraingen.evaluate(terraingen.Terrain(terraingen.FlatSource(257, 257, PIXEL), [layer])).heights.values
        print(f"{name:12s} done")
        shades.append(hillshade(heights, PIXEL))
    print("wrote", write_png(out / "example_craters_irregularity.png", montage(shades, columns=4)))


if __name__ == "__main__":
    main()
