"""Crater profiles: the shape of a crater across its centre, and a profile of your own.

Plots the height across a 100 m crater for each built-in profile (Fassett and Thomson's fresh
profile with its three rim fits, Martin, Parkes and Dunstan's, and the same fresh profile after
topographic diffusion) and for a parabolic bowl written in Python by subclassing `CraterProfile`.
Then generates the same craters with each profile and writes a montage of hillshades.

    python example_craters_profiles.py [OUTPUT_DIR]
"""

import pathlib
import sys

import numpy as np

import terraingen

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
from _common import colour_name, hillshade, line_plot, montage, output_dir, write_png

DIAMETER = 100.0


class ParabolicBowl(terraingen.CraterProfile):
    """A paraboloid bowl with a raised rim falling off as (r / R)^-3 outside it."""

    def height(self, distance, diameter, state):
        radius = diameter / 2
        depth = state.current.depth_ratio * diameter
        rim = state.current.rim_height_ratio * diameter
        x = distance / radius
        if x <= 1.0:
            return rim - (depth) * (1.0 - x * x)
        return rim * x**-3

    def reach_radii(self):
        return 2.0


def main():
    out = output_dir()
    fresh = terraingen.lunar_fresh_crater_morphometry(DIAMETER)
    state = terraingen.CraterState(fresh, fresh, 0.0)
    diffused = terraingen.DiffusionDegradation(
        terraingen.fassett_thomson_2014_diffusivity(), terraingen.DiffusedProfile(terraingen.FassettThomsonProfile())
    )
    old_state = diffused.state(terraingen.Crater(0.0, 0.0, DIAMETER, age_gyr=0.1))
    profiles = {
        "Fassett-Thomson, original rim": (terraingen.FassettThomsonProfile(terraingen.FassettThomsonRim.ORIGINAL), state),
        "Fassett-Thomson, rim at one radius": (terraingen.FassettThomsonProfile(terraingen.FassettThomsonRim.AT_ONE_RADIUS), state),
        "Fassett-Thomson, plateau rim": (terraingen.FassettThomsonProfile(terraingen.FassettThomsonRim.PLATEAU), state),
        "Martin-Parkes-Dunstan": (terraingen.MartinParkesDunstanProfile(), state),
        "diffused for 0.1 Gyr": (terraingen.DiffusedProfile(terraingen.FassettThomsonProfile()), old_state),
        "custom parabolic bowl": (ParabolicBowl(), state),
    }
    distances = np.linspace(0.0, 1.6 * DIAMETER, 400)
    curves = []
    for name, (profile, crater_state) in profiles.items():
        heights = np.array([profile.height(d, DIAMETER, crater_state) for d in distances])
        print(f"{name:36s} floor {heights[0]:7.2f} m, highest {heights.max():5.2f} m")
        curves.append((np.concatenate([-distances[::-1], distances]), np.concatenate([heights[::-1], heights])))
    print("legend:", ", ".join(f"{colour_name(i)} {n}" for i, n in enumerate(profiles)))
    print("wrote", write_png(out / "example_craters_profiles_curves.png", line_plot(curves)))

    surface = terraingen.lunar_surface(3.5)
    generation = terraingen.CraterGenerationOptions(min_diameter=8.0, max_diameter=150.0)
    shades = []
    for profile, _ in profiles.values():
        if isinstance(profile, terraingen.DiffusedProfile):
            continue
        models = terraingen.CraterModels(surface.density, surface.ages, terraingen.FreshnessDegradation(surface.ages), profile)
        craters = terraingen.CraterLayer(models, terraingen.CraterLayerOptions(seed=2, generation=generation))
        heights = terraingen.evaluate(terraingen.Terrain(terraingen.FlatSource(257, 257, 1.0), [craters])).heights.values
        shades.append(hillshade(heights, 1.0))
    print("wrote", write_png(out / "example_craters_profiles.png", montage(shades, columns=5)))


if __name__ == "__main__":
    main()
