"""How craters are applied to rough terrain: datum, interior replacement and blend.

By default a crater's profile is added to the terrain, so its floor follows the rough terrain
beneath. A `datum` instead cuts the interior relative to the ground fitted under the crater: a
plane, its mean height or the height at its centre. With a datum, `interior_replacement` sets how
much of the interior replaces the older relief (1 erases it, as an impact excavates it; 0 keeps it)
and `blend` where the replaced interior gives way to the draped rim. Writes a montage of hillshades
of the same craters on the same rough terrain.

    python example_craters_apply_options.py [OUTPUT_DIR]
"""

import terraingen
from _common import hillshade, montage, output_dir, write_png

PIXEL = 2.0


def main():
    out = output_dir()
    surface = terraingen.lunar_surface(3.5)
    models = terraingen.CraterModels(
        surface.density, surface.ages, terraingen.FreshnessDegradation(surface.ages), terraingen.FassettThomsonProfile()
    )
    rough = terraingen.NoiseRelief([terraingen.NoiseLayer(terraingen.FractalParams(seed=1, wavelength=150.0, amplitude=25.0))])
    options = {
        "default": terraingen.CraterApplyOptions(),
        "plane datum": terraingen.CraterApplyOptions(datum=terraingen.CraterDatum.PLANE),
        "mean-height datum": terraingen.CraterApplyOptions(datum=terraingen.CraterDatum.MEAN_HEIGHT),
        "centre-height datum": terraingen.CraterApplyOptions(datum=terraingen.CraterDatum.CENTRE_HEIGHT),
        "plane, half replaced": terraingen.CraterApplyOptions(datum=terraingen.CraterDatum.PLANE, interior_replacement=0.5),
        "plane, wide blend": terraingen.CraterApplyOptions(
            datum=terraingen.CraterDatum.PLANE,
            blend=terraingen.CraterBlend(start_radii=0.5, end_radii=1.5, shape=terraingen.BlendShape.SMOOTHSTEP),
        ),
    }
    generation = terraingen.CraterGenerationOptions(min_diameter=20.0, max_diameter=300.0)
    shades = []
    for name, apply in options.items():
        layer = terraingen.CraterLayer(models, terraingen.CraterLayerOptions(seed=1, generation=generation, apply=apply))
        heights = terraingen.evaluate(terraingen.Terrain(terraingen.FlatSource(257, 257, PIXEL), [rough, layer])).heights.values
        print(f"{name:20s} heights {heights.min():7.1f} to {heights.max():6.1f} m")
        shades.append(hillshade(heights, PIXEL))
    print("wrote", write_png(out / "example_craters_apply_options.png", montage(shades, columns=3)))


if __name__ == "__main__":
    main()
