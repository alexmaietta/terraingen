# SPDX-License-Identifier: BSD-3-Clause
"""Write a Crater Profile of Your Own."""

import terraingen


def write_a_crater_profile():
    # --8<-- [start:write_a_crater_profile]
    class ConeProfile(terraingen.CraterProfile):
        """A straight-walled cone to the rim, and a rim that falls off as the cube of distance."""

        def height(self, distance, diameter, state):
            x = distance / (0.5 * diameter)
            depth = state.current.depth_ratio * diameter
            rim = state.current.rim_height_ratio * diameter
            return rim - depth * (1.0 - x) if x <= 1.0 else rim * x**-3

        def reach_radii(self):
            return 2.0  # how far from the center it changes the ground, in radii

    models = terraingen.CraterModels(
        terraingen.PowerLawSfd(0.05, 2.0),
        terraingen.FreshAges(),
        terraingen.NoDegradation(terraingen.lunar_fresh_crater_morphometry),
        ConeProfile(),
    )
    options = terraingen.CraterLayerOptions(
        seed=1, generation=terraingen.CraterGenerationOptions(d_min=6.0, d_max=60.0)
    )
    craters = terraingen.CraterLayer(models, options)
    heights = terraingen.Terrain(source=terraingen.FlatSource(129, 129, 1.0), layers=[craters]).evaluate()
    # --8<-- [end:write_a_crater_profile]
    return heights, models


def test_write_a_crater_profile():
    heights, models = write_a_crater_profile()
    assert heights.values.min() < 0.0
    assert isinstance(models.profile, terraingen.CraterProfile)


def figure(quick=False):
    """Cone-shaped craters, hillshaded (left), and a fresh 100 m crater's height over diameter
    with the cone and with the default profile of Martin, Parkes and Dunstan (right)."""
    import gallery
    import matplotlib.pyplot as plt
    import numpy as np

    heights, models = write_a_crater_profile()
    diameter = 100.0
    radii = np.linspace(0.0, 2.0, 401)
    default = terraingen.default_crater_models()
    profiles = {"Cone": (models, "-"), "Default (Martin, Parkes and Dunstan)": (default, "--")}
    fig, (shaded, curves) = plt.subplots(1, 2, figsize=(10, 4.2), layout="constrained", width_ratios=[1, 1.4])
    gallery.show_hillshade(shaded, heights.values, 1.0)
    for name, (crater_models, style) in profiles.items():
        state = crater_models.degradation.state(terraingen.Crater(0.0, 0.0, diameter, age_gyr=0.0))
        curve = [crater_models.profile.height(r * 0.5 * diameter, diameter, state) for r in radii]
        curves.plot(radii, np.asarray(curve) / diameter, lw=1.6, ls=style, label=name)
    curves.axhline(0.0, color="#b5b5b3", lw=0.6)
    curves.set_xlabel("Distance from center (radii)")
    curves.set_ylabel("Height / diameter")
    curves.legend(loc="lower right")
    return fig
