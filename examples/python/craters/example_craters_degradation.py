"""Compare crater degradation models on the same crater population.

`NoDegradation` keeps each crater's fresh depth (or a fixed depth ratio); `FreshnessDegradation`
makes it shallower with its age relative to its lifetime; `DiffusionDegradation` smooths its shape by
topographic diffusion over its age (Fassett and Thomson 2014), with the diffusivity of Fassett and
Thomson (2014) or of Fassett et al. (2022). The craters are the same in every panel. Prints each
model's mean depth ratio and writes a montage of hillshades.

    python example_craters_degradation.py [OUTPUT_DIR]
"""

import pathlib
import sys

import terraingen

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
from _common import hillshade, montage, output_dir, write_png

PIXEL = 1.0


def main():
    out = output_dir()
    surface = terraingen.lunar_surface(3.5)
    diffused = terraingen.DiffusedProfile(terraingen.FassettThomsonProfile())
    models = {
        "none": terraingen.NoDegradation(),
        "fixed d/D 0.1": terraingen.NoDegradation(depth_ratio=0.1),
        "freshness": terraingen.FreshnessDegradation(surface.ages, cutoff_depth_ratio=0.04),
        "diffusion FT2014": terraingen.DiffusionDegradation(terraingen.fassett_thomson_2014_diffusivity(), diffused),
        "diffusion F2022": terraingen.DiffusionDegradation(terraingen.fassett_2022_hartmann_diffusivity(), diffused),
    }
    generation = terraingen.CraterGenerationOptions(min_diameter=4.0, max_diameter=150.0)
    shades = []
    for name, degradation in models.items():
        crater_models = terraingen.CraterModels(surface.density, surface.ages, degradation, terraingen.FassettThomsonProfile())
        craters = terraingen.CraterLayer(crater_models, terraingen.CraterLayerOptions(seed=31, generation=generation))
        evaluated = terraingen.evaluate(terraingen.Terrain(terraingen.FlatSource(257, 257, PIXEL), [craters]))
        print(f"{name:18s} mean depth/diameter {evaluated.craters['depth_ratio'].mean():.3f}")
        shades.append(hillshade(evaluated.heights.values, PIXEL))
    print("wrote", write_png(out / "example_craters_degradation.png", montage(shades, columns=5)))


if __name__ == "__main__":
    main()
