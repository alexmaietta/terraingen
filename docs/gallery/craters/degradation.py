# SPDX-License-Identifier: BSD-3-Clause

"""The same craters under each degradation model: kept fresh, made shallower with age, or diffused."""

import gallery
import matplotlib.pyplot as plt

import terraingen


def figure(quick=False):
    size = 129 if quick else 385
    surface = terraingen.EquilibriumSurface(
        terraingen.extended_neukum_production_function(),
        terraingen.trask_equilibrium(),
        3.5,
        terraingen.neukum_lunar_chronology,
    )
    ages = surface.ages()
    fresh = terraingen.lunar_fresh_crater_morphometry
    diffused = terraingen.DiffusedProfile(terraingen.FassettThomsonProfile())
    models = {
        "None": terraingen.NoDegradation(fresh),
        "Freshness": terraingen.FreshnessDegradation(ages, fresh),
        "Diffusion, Fassett and Thomson (2014)": terraingen.DiffusionDegradation(
            fresh, terraingen.fassett_thomson_2014_diffusivity(), diffused
        ),
        "Diffusion, Fassett et al. (2022)": terraingen.DiffusionDegradation(
            fresh, terraingen.fassett_2022_hartmann_diffusivity(), diffused
        ),
    }
    options = terraingen.CraterLayerOptions(
        seed=31, generation=terraingen.CraterGenerationOptions(d_min=4.0, d_max=150.0)
    )
    fig, axes = plt.subplots(1, len(models), figsize=(12, 3.3), layout="constrained")
    for ax, (title, degradation) in zip(axes, models.items(), strict=True):
        profile = None if degradation.required_profile() else terraingen.FassettThomsonProfile()
        craters = terraingen.CraterLayer(
            terraingen.CraterModels(surface.density(), ages, degradation, profile), options
        )
        terrain = terraingen.TerrainGenerator(terraingen.FlatSource(size, size, 1.0), [craters])
        gallery.show_hillshade(ax, terraingen.evaluate(terrain).values, 1.0)
        ax.set_title(title)
    return fig
