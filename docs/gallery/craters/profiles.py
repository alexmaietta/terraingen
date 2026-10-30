# SPDX-License-Identifier: BSD-3-Clause

"""Fresh 100 m crater profiles compare Martin, Parkes and Dunstan's polynomials with Fassett and
Thomson's cubic fits with their three rims, the original fits meeting at 0.98 radii, synthterrain's rim
at one radius, and Fassett's (2022) flat plateau. The top row shows height across the crater, and
zoomed on the rim. Below, each profile is a round crater on flat ground, hillshaded and as a slope
map, where the Fassett and Thomson walls show as one even slope and their rims as sharp breaks."""

import gallery
import matplotlib.pyplot as plt
import numpy as np

import terraingen

WIDE = True
SECTION = "Profiles"

DIAMETER = 100.0
SIZE_M = 260.0
PROFILES = [
    ("Martin, Parkes and Dunstan", terraingen.MartinParkesDunstanProfile()),
    (
        "Fassett and Thomson, original",
        terraingen.FassettThomsonProfile(terraingen.FassettThomsonRim.ORIGINAL),
    ),
    (
        "Fassett and Thomson, at one radius",
        terraingen.FassettThomsonProfile(terraingen.FassettThomsonRim.AT_ONE_RADIUS),
    ),
    ("Fassett and Thomson, plateau", terraingen.FassettThomsonProfile(terraingen.FassettThomsonRim.PLATEAU)),
]


def figure(quick=False):
    pixel = 2.0 if quick else 0.25
    size = int(SIZE_M / pixel) + 1
    fresh = terraingen.lunar_fresh_crater_morphometry(DIAMETER)
    state = terraingen.CraterState(fresh, fresh)
    radii = np.linspace(0.0, 2.0, 200 if quick else 800)
    radius = 0.5 * DIAMETER

    fig = plt.figure(figsize=(14, 12), layout="constrained")
    curves, maps = fig.subfigures(2, 1, height_ratios=[1, 1.55])
    whole, rim = curves.subplots(1, 2, width_ratios=[2, 1])
    map_axes = maps.subplots(2, len(PROFILES))
    for column, (name, profile) in enumerate(PROFILES):
        heights = np.array([profile.height(r * radius, DIAMETER, state) for r in radii])
        whole.plot(radii, heights / DIAMETER, label=name, lw=1.4)
        rim.plot(radii, heights / DIAMETER, lw=1.4)

        models = terraingen.CraterModels(
            None,
            terraingen.FreshAges(),
            terraingen.NoDegradation(terraingen.lunar_fresh_crater_morphometry),
            profile,
        )
        crater = terraingen.CraterLayer.manual([terraingen.Crater(0.0, 0.0, DIAMETER, age_gyr=0.0)], models)
        terrain = terraingen.Terrain(source=terraingen.FlatSource(size, size, pixel), layers=[crater])
        surface = terrain.evaluate().values
        gallery.show_hillshade(map_axes[0, column], surface, pixel)
        image = gallery.show_slopes(map_axes[1, column], surface, pixel, vmax=40.0, colorbar=False)
        map_axes[0, column].set_title(name)
    maps.colorbar(image, ax=map_axes[1], label="Slope (degrees)", shrink=0.85)

    whole.set_xlabel("Distance from center (radii)")
    whole.set_ylabel("Height / diameter")
    whole.axhline(0.0, color="#b5b5b3", lw=0.6)
    whole.legend(loc="lower right")
    rim.set_xlim(0.85, 1.25)
    rim.set_ylim(0.0, 1.15 * fresh.rim_height_ratio)
    rim.set_xlabel("Distance from center (radii)")
    rim.set_title("At the rim")
    return fig
