"""Crater profiles: the shape of a crater across its center, and a profile of your own.

Plots the height across a 100 m crater for each built-in profile (Fassett and Thomson's fresh
profile with its three rim fits, Martin, Parkes and Dunstan's, and the same fresh profile after
topographic diffusion) and for a parabolic bowl written in Python by subclassing `CraterProfile`.
Then generates the same craters with each profile and writes a montage of hillshades.

    python example_craters_profiles.py [--viewer window|null] [--output-dir DIR] [--test]
"""

import pathlib
import sys

import numpy as np

import terraingen

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
import _common
from _common import color_name, hillshade, line_plot, montage, write_png

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


class Example(_common.Example):
    def generate(self):
        fresh = terraingen.lunar_fresh_crater_morphometry(DIAMETER)
        state = terraingen.CraterState(fresh, fresh, 0.0)
        diffused = terraingen.DiffusionDegradation(
            terraingen.lunar_fresh_crater_morphometry,
            terraingen.fassett_thomson_2014_diffusivity(),
            terraingen.DiffusedProfile(terraingen.FassettThomsonProfile()),
        )
        old_state = diffused.state(terraingen.Crater(0.0, 0.0, DIAMETER, age_gyr=0.1))
        profiles = {
            "Fassett-Thomson, original rim": (
                terraingen.FassettThomsonProfile(terraingen.FassettThomsonRim.ORIGINAL),
                state,
            ),
            "Fassett-Thomson, rim at one radius": (
                terraingen.FassettThomsonProfile(terraingen.FassettThomsonRim.AT_ONE_RADIUS),
                state,
            ),
            "Fassett-Thomson, plateau rim": (
                terraingen.FassettThomsonProfile(terraingen.FassettThomsonRim.PLATEAU),
                state,
            ),
            "Martin-Parkes-Dunstan": (terraingen.MartinParkesDunstanProfile(), state),
            "diffused for 0.1 Gyr": (
                terraingen.DiffusedProfile(terraingen.FassettThomsonProfile()),
                old_state,
            ),
            "custom parabolic bowl": (ParabolicBowl(), state),
        }
        distances = np.linspace(0.0, 1.6 * DIAMETER, 400)
        self.curves = []
        self.profile_heights = {}
        for name, (profile, crater_state) in profiles.items():
            heights = np.array([profile.height(d, DIAMETER, crater_state) for d in distances])
            print(f"{name:36s} floor {heights[0]:7.2f} m, highest {heights.max():5.2f} m")
            self.profile_heights[name] = heights
            self.curves.append(
                (np.concatenate([-distances[::-1], distances]), np.concatenate([heights[::-1], heights]))
            )
        print("legend:", ", ".join(f"{color_name(i)} {n}" for i, n in enumerate(profiles)))

        production = terraingen.extended_neukum_production_function()
        equilibrium = terraingen.trask_equilibrium()
        density = terraingen.MinimumSfd(
            [terraingen.ProductionSurface(production, 3.5, terraingen.neukum_lunar_chronology), equilibrium]
        )
        ages = terraingen.SteadyStateAges(production, equilibrium, 3.5, terraingen.neukum_lunar_chronology)
        generation = terraingen.CraterGenerationOptions(d_min=8.0, d_max=150.0)
        self.terrains = {}
        for name, (profile, _) in profiles.items():
            if isinstance(profile, terraingen.DiffusedProfile):
                continue
            models = terraingen.CraterModels(
                density,
                ages,
                terraingen.FreshnessDegradation(ages, terraingen.lunar_fresh_crater_morphometry),
                profile,
            )
            craters = terraingen.CraterLayer(
                models, terraingen.CraterLayerOptions(seed=2, generation=generation)
            )
            heights = terraingen.evaluate(
                terraingen.TerrainGenerator(terraingen.FlatSource(257, 257, 1.0), [craters])
            )
            self.terrains[name] = heights.values
            self.show(name, heights)

    def save(self, out):
        print("wrote", write_png(out / "example_craters_profiles_curves.png", line_plot(self.curves)))
        shades = [hillshade(heights, 1.0) for heights in self.terrains.values()]
        print("wrote", write_png(out / "example_craters_profiles.png", montage(shades, columns=5)))

    def test_final(self):
        profiles = self.profile_heights
        assert len(profiles) == 6 and len(self.terrains) == 5, "six profiles plotted, five generated"
        assert all(np.all(np.isfinite(heights)) for heights in profiles.values()), "every profile is finite"
        assert all(heights[0] < 0.0 for heights in profiles.values()), (
            "every crater has a floor below the datum"
        )
        assert all(
            heights.max() > 1.0 for name, heights in profiles.items() if name != "diffused for 0.1 Gyr"
        ), "every fresh crater has a rim above the datum"
        fresh = profiles["Fassett-Thomson, original rim"]
        diffused = profiles["diffused for 0.1 Gyr"]
        assert diffused[0] > fresh[0] and diffused.max() < fresh.max(), (
            "diffusion raises the floor and lowers the rim"
        )
        assert all(
            heights.shape == (257, 257) and np.all(np.isfinite(heights)) for heights in self.terrains.values()
        ), "every generated terrain is 257 x 257 and finite"


if __name__ == "__main__":
    _common.main(Example)
