# SPDX-License-Identifier: BSD-3-Clause

"""Complex crater profiles, scaled by diameter. Left: fresh lunar craters 10 km across, still a
simple bowl, then 20, 40 and 80 km past the 15 km transition, with flat floors widening, walls
shallowing and central peaks rising. Middle: a fresh 40 km crater on the Moon beside one on Mars,
whose complex craters start at 7 km and are shallower. Right: the 40 km lunar crater worn to 75% and
50% of its fresh depth, its peak lowered with the rest of it."""

import matplotlib.pyplot as plt
import numpy as np

import terraingen

WIDE = True
SECTION = "Profiles"

DIAMETERS_KM = [10.0, 20.0, 40.0, 80.0]
WORN_DEPTHS = [1.0, 0.75, 0.5]


def profile_curve(profile, diameter, state, radii):
    return np.array([profile.height(r * 0.5 * diameter, diameter, state) for r in radii]) / diameter


def figure(quick=False):
    radii = np.linspace(0.0, 2.5, 200 if quick else 800)
    simple = terraingen.MartinParkesDunstanProfile()
    moon = terraingen.ComplexCraterProfile(simple, terraingen.lunar_complex_crater_shape())
    mars = terraingen.ComplexCraterProfile(simple, terraingen.mars_complex_crater_shape())

    fig, (sizes_ax, bodies_ax, worn_ax) = plt.subplots(
        1, 3, figsize=(17, 4.8), layout="constrained", sharey=True
    )
    colors = plt.get_cmap("viridis")(np.linspace(0.0, 0.85, len(DIAMETERS_KM)))
    for diameter_km, color in zip(DIAMETERS_KM, colors, strict=True):
        diameter = diameter_km * 1000.0
        fresh = terraingen.lunar_fresh_crater_morphometry(diameter)
        kind = "complex" if moon.is_complex(diameter) else "simple"
        sizes_ax.plot(
            radii,
            profile_curve(moon, diameter, terraingen.CraterState(fresh, fresh), radii),
            color=color,
            lw=1.6,
            label=f"{diameter_km:g} km, {kind}",
        )
    sizes_ax.set_title("Moon, by diameter")

    diameter = 40000.0
    for name, profile, morphometry in (
        ("Moon", moon, terraingen.lunar_fresh_crater_morphometry),
        ("Mars", mars, terraingen.mars_fresh_crater_morphometry),
    ):
        fresh = morphometry(diameter)
        bodies_ax.plot(
            radii,
            profile_curve(profile, diameter, terraingen.CraterState(fresh, fresh), radii),
            lw=1.6,
            label=f"{name}, d/D {fresh.depth_ratio:.3f}",
        )
    bodies_ax.set_title("40 km, Moon and Mars")

    fresh = terraingen.lunar_fresh_crater_morphometry(diameter)
    for share in WORN_DEPTHS:
        current = terraingen.CraterMorphometry(share * fresh.depth_ratio, share * fresh.rim_height_ratio)
        worn_ax.plot(
            radii,
            profile_curve(moon, diameter, terraingen.CraterState(fresh, current), radii),
            lw=1.6,
            label=f"{share:.0%} of fresh depth",
        )
    worn_ax.set_title("40 km lunar, worn")

    for ax in (sizes_ax, bodies_ax, worn_ax):
        ax.axhline(0.0, color="#b5b5b3", lw=0.6)
        ax.set_xlabel("Distance from center (radii)")
        ax.legend(loc="lower right", fontsize=8)
    sizes_ax.set_ylabel("Height / diameter")
    return fig
