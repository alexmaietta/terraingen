# SPDX-License-Identifier: BSD-3-Clause

"""Crater chronologies: the density of craters 1 km and larger a surface collects over its age, on the
Moon (Neukum) and Mars (Ivanov). Left: rising steeply through the late heavy bombardment before 3.5
Gyr ago, then almost linearly. Right: the production surfaces they scale, extended Neukum production
on surfaces 10 Myr to 3.9 Gyr old."""

import matplotlib.pyplot as plt
import numpy as np

import terraingen

WIDE = True

AGES_GYR = [0.01, 0.1, 1.0, 3.0, 3.9]


def figure(quick=False):
    ages = np.linspace(0.0, 4.5, 100 if quick else 400)
    diameters = np.geomspace(2.5, 10000.0, 100 if quick else 300)

    fig, (chronology, surfaces) = plt.subplots(1, 2, figsize=(14, 5), layout="constrained")
    for name, function in (
        ("Moon, Neukum", terraingen.neukum_lunar_chronology),
        ("Mars, Ivanov", terraingen.ivanov_mars_chronology),
    ):
        chronology.plot(ages, [function(age) * 1.0e6 for age in ages], lw=1.8, label=name)
    chronology.set_yscale("log")
    chronology.set_xlabel("Surface age (Gyr)")
    chronology.set_ylabel("Craters 1 km and up per km$^2$")
    chronology.invert_xaxis()
    chronology.legend()

    colors = plt.get_cmap("viridis")(np.linspace(0.0, 0.85, len(AGES_GYR)))
    for age, color in zip(AGES_GYR, colors, strict=True):
        surface = terraingen.ProductionSurface(
            terraingen.extended_neukum_production_function(), age, terraingen.neukum_lunar_chronology
        )
        surfaces.plot(
            diameters,
            [surface.cumulative_at(d) for d in diameters],
            color=color,
            lw=1.6,
            label=f"{age * 1000:g} Myr" if age < 1.0 else f"{age:g} Gyr",
        )
    surfaces.set_xscale("log")
    surfaces.set_yscale("log")
    surfaces.set_xlabel("Crater diameter D (m)")
    surfaces.set_ylabel("Craters at least D across per m$^2$")
    surfaces.legend(title="Surface age")
    return fig
